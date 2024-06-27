#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <libpq-fe.h>
#include <ctype.h>

static int verbose_flag;
static int port = -1;
const char *log_path = "application.log";

const int BUFFER_SIZE = 2048;

static char ip_address[INET_ADDRSTRLEN] = "";

char conninfo[BUFFER_SIZE]; 

typedef struct {
    int list_logements : 1;
    int calendrier_disponibilite : 1;
    int mise_indispo : 1;
    int admin : 1;
} Permissions;



typedef struct {
    char id[50];
    char name[256];
    Permissions perms;
} User;

User* authenticate(const char* api_key);

void output_log(const char *msg);
void error(const char *msg, int isFromLog);
void help();
void launch_socket();
PGresult* request(const char *sql, const char **paramValues, int paramCount);
const char* pg_get_attribute(PGresult *res, int row, const char *attribute_name);
void list_all(int cnx, User *usr);
void get_planning(int cnx, User *usr, const char *buffer);
int validate_date(const char* input);
void set_availability(int cnx, User *usr, const char *buffer);

Permissions extract_permissions(const char* permission_string) {
    Permissions perms = {0};
    if (permission_string == NULL || strlen(permission_string) < 4) {
        output_log("[Permission] Impossible to read permission correctly.");
    }
    perms.admin = permission_string[0] == '1';
    perms.mise_indispo = permission_string[1] == '1';
    perms.calendrier_disponibilite = permission_string[2] == '1';
    perms.list_logements = permission_string[3] == '1';

    return perms;
}

static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"port", required_argument, 0, 'p'},
    {"verbose", no_argument, 0, 'v'},
    {"log", required_argument, 0, 'l'},
    {0, 0, 0, 0}
};

#define MAX_LINE_LENGTH 256

char *trim_whitespace(char *str) {
    char *end;

    while (*str == ' ' || *str == '"'){
        str++;
    } 

    if (*str == 0) 
        return str;

    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '"')) end--;

    *(end + 1) = 0;

    return str;
}

void parse_env_file(const char *filename, char *host, char *dbname, char *user, char *password) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Could not open .env file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        char *trimmed_line = trim_whitespace(line);

        if (strncasecmp(trimmed_line, "DB_SERVER=", 10) == 0) {
            strcpy(host, trim_whitespace(trimmed_line + 10));
        } else if (strncasecmp(trimmed_line, "DB_NAME=", 8) == 0) {
            strcpy(dbname, trim_whitespace(trimmed_line + 8));
        } else if (strncasecmp(trimmed_line, "DB_USER=", 8) == 0) {
            strcpy(user, trim_whitespace(trimmed_line + 8));
        } else if (strncasecmp(trimmed_line, "DB_PASS=", 8) == 0) {
            strcpy(password, trim_whitespace(trimmed_line + 8));
        }
    }

    fclose(file);
}


int main(int argc, char *argv[]) {
    int opt;
    int opt_index = 0;

    while ((opt = getopt_long(argc, argv, "hp:vl:", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 'h':
                help();
                exit(EXIT_SUCCESS);
            case 'p':
                port = atoi(optarg);
                printf("[OPTION] Port defined to : %d\n", port);
                break;
            case 'v':
                verbose_flag = 1;
                printf("[OPTION] Verbose mode defined\n");
                break;
            case 'l':
                log_path = optarg;
                printf("[OPTION] Log file set to %s\n", log_path);
                break;
            default:
                help();
                exit(EXIT_FAILURE);
        }
    }
    if (port <= 0) {
        printf("Error: Port must be defined.\n");
        help();
        exit(EXIT_FAILURE);
    }

    char host[128] = {0};
    char dbname[128] = {0};
    char user[128] = {0};
    char password[128] = {0};

    parse_env_file(".env", host, dbname, user, password);

    if (strlen(host) == 0 || strlen(dbname) == 0 || strlen(user) == 0 || strlen(password) == 0) {
        printf("One or more environment variables are missing\n");
        return 1;
    }

    snprintf(conninfo, sizeof(conninfo), "host=%s dbname=%s user=%s password=%s", host, dbname, user, password);


    launch_socket();
    return 0;
}

void error(const char *msg, int isFromLog) {
    if (verbose_flag && !isFromLog) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "%s: %s", msg, strerror(errno));
        output_log(buffer);
        exit(EXIT_FAILURE);
    } else {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

void output_log(const char *msg) {
    if (verbose_flag) {
        FILE *log_file = fopen(log_path, "a+");
        if (log_file == NULL) {
            error("Error when opening log file.", 1);
        }

        time_t now = time(NULL);
        struct tm *t = localtime(&now);

        char time_str[100];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

        char log_msg[BUFFER_SIZE];
        if (strlen(ip_address) > 0) {
            snprintf(log_msg, sizeof(log_msg), "[%s] [IP: %s] %s", time_str, ip_address, msg);
        } else {
            snprintf(log_msg, sizeof(log_msg), "[%s] %s", time_str, msg);
        }

        fprintf(log_file, "%s\n", log_msg);
        fclose(log_file);

        printf("%s\n", log_msg);
        memset(log_msg, 0, BUFFER_SIZE);
    }
}



void help() {
    printf("Usage: ./server [options] --port <port>\n");
    printf("  --%-*s  %s\n", 7, "help", "Show the different options available for this command.");
    printf("  --%-*s  %s\n", 7, "verbose", "Log entirely the server.");
    printf("  --%-*s  %s\n", 7, "log", "Define the file for the log output, default is application.log");
}

void clean_input(char *str) {
    if (str == NULL) return;

    char *src = str, *dst = str;
    while (*src) {
        if (isprint((unsigned char)*src)) {
            *dst = *src;
            dst++;
        }
        src++;
    }
    *dst = '\0';
}

void launch_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    int size;
    int cnx;
    char log_msg[BUFFER_SIZE]; // buffer pour les logs
    char response[BUFFER_SIZE]; // buffer pour les responses
    struct sockaddr_in addr;
    char formatter[BUFFER_SIZE]; // buffer pour formatter des chaines temporairement
    char buffer[BUFFER_SIZE]; // buffer pour les saisies

    struct sockaddr_in conn_addr;
    User *user = NULL;
    
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error("Socket Initialization", 0);
    }

    if (listen(sock, 1) < 0) {
        error("Socket Initialization", 0);
    }

    snprintf(log_msg, BUFFER_SIZE, "[Socket] Listening on port: %d", port);
    output_log(log_msg);

    size = sizeof(conn_addr);

    printf("Waiting for connection...\n");

    while(1) {
        inet_ntop(AF_INET, &conn_addr.sin_addr, ip_address, INET_ADDRSTRLEN);

        if ((cnx = accept(sock, (struct sockaddr *)&conn_addr, (socklen_t *)&size)) < 0) {
            error("Socket Initialization", 0);
        }
        output_log("[Socket] New connection");

        while(1) {
            user = NULL;
            do {
                memset(buffer, 0, sizeof(buffer));
                if (write(cnx, "WAIT AUTH\n", 11) <= 0) break;
                output_log("Waiting for API key...");
                int valread = read(cnx, buffer, BUFFER_SIZE);
                if (valread <= 0) break;
                clean_input(buffer);
                
                snprintf(log_msg, BUFFER_SIZE, "API Key received : %s", buffer);
                output_log(log_msg);
                user = authenticate(buffer);

                if (user == NULL){
                    snprintf(log_msg, BUFFER_SIZE, "AUTH REFUSED (%s)", buffer);
                    output_log(log_msg);
                    strcat(log_msg, "\n");
                    if (send(cnx, log_msg, strlen(log_msg), 0) <= 0) break;
                }
                memset(buffer, 0, sizeof(buffer));
            } while(user == NULL);

            if (user == NULL) break;

            snprintf(log_msg, BUFFER_SIZE, "[Authentification] API Key OK (%s)", user->name);
            output_log(log_msg);

            snprintf(response, BUFFER_SIZE, "AUTH OK %s\n", user->name);
            if (send(cnx, response, strlen(response), 0) <= 0) break;

            while (1) {
                if (send(cnx, "WAIT ACTION\n", 13, 0) <= 0) break;
                output_log("Waiting for Command...");
                int valread = read(cnx, buffer, BUFFER_SIZE);
                if (valread <= 0) break;

                memset(response, 0, sizeof(response));
                memset(log_msg, 0, sizeof(log_msg));

                snprintf(log_msg, BUFFER_SIZE, "[Command] Received %s", buffer);
                output_log(log_msg);

                if (strncasecmp(buffer, "LIST_ALL", 8) == 0) {
                    list_all(cnx, user);
                } else if (strncasecmp(buffer, "GET_PLANNING", 12) == 0) {
                    get_planning(cnx, user, buffer);
                } else if (strncasecmp(buffer, "HELP", 4) == 0) {
                    snprintf(response, BUFFER_SIZE, "%-*s  %s\n", 36, "LIST_ALL", "List all logement.");
                    snprintf(formatter, BUFFER_SIZE, "%-*s  %s\n", 36, "GET_PLANNING <ID> <DEBUT> [FIN]", "List planing of specified logement. <ID>: Identifiant du logement, <DEBUT>: Date de début, [FIN]; Date de fin (optionnel).");
                    strcat(response, formatter);
                    snprintf(formatter, BUFFER_SIZE, "%-*s  %s\n", 36, "HELP", "Show the help.");
                    strcat(response, formatter);
                    snprintf(formatter, BUFFER_SIZE, "%-*s  %s\n", 36, "QUIT", "Quit the syslog.");                
                    strcat(response, formatter);
                    if (send(cnx, response, strlen(response), 0) <= 0) break;
                } else if (strncasecmp(buffer, "SET_AVAILABILITY", 16) == 0) {
                    set_availability(cnx, user, buffer);
                } else if (strncasecmp(buffer, "QUIT", 4) == 0) {
                    break;
                } else {
                    memset(log_msg, 0, sizeof(log_msg));
                    if (send(cnx, "ACTION NOT FOUND\n", 18, 0) <= 0) break;
                    snprintf(log_msg, BUFFER_SIZE, "[Command] Unknown Command (%s)", buffer);
                    output_log(log_msg);
                }
            }
            
            if (user != NULL) {
                free(user);
                user = NULL;
            }
            break; 
        }
        
        close(cnx);
        output_log("[Socket] Disconnection");
    }
}

PGresult* request(const char *sql, const char **paramValues, int paramCount) {
    PGconn *conn = PQconnectdb(conninfo);
    char buffer[BUFFER_SIZE];
    if (PQstatus(conn) != CONNECTION_OK) {
        snprintf(buffer, sizeof(buffer), "Connection to database failed: %s", PQerrorMessage(conn));
        output_log(buffer);
        PQfinish(conn);
        return NULL;
    }
    PGresult *res;

    if (paramCount > 0){
        res = PQexecParams(conn, sql, paramCount, NULL, paramValues, NULL, NULL, 0);
    } else {
        res = PQexec(conn, sql);
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        snprintf(buffer, sizeof(buffer), "Connection to database failed: %s", PQerrorMessage(conn));
        output_log(buffer);
        PQclear(res);
        PQfinish(conn);
        return NULL;
    }

    PQfinish(conn);
    return res;
}


const char* pg_get_attribute(PGresult *res, int row, const char *attribute_name) {
    int nFields = PQnfields(res);
    for (int i = 0; i < nFields; i++) {
        if (strcmp(PQfname(res, i), attribute_name) == 0) {
            if (PQntuples(res) > 0) {
                return PQgetvalue(res, row, i);
            }
        }
    }
    return NULL;
}

void list_all(int cnx, User *usr) {
    if (!usr->perms.list_logements) {
        send(cnx, "Permission Denied.\n", 20, 0);
        return;
    }

    const char *sql;
    const char *paramValues[1];
    int paramCount;

    if (usr->perms.admin){
        sql = "SELECT id, titre FROM sae._logement;";
        paramCount = 0;
    } else {
        sql = "SELECT id, titre FROM sae._logement WHERE id_proprietaire = $1;";
        paramValues[0] = usr->id;
        paramCount = 1;
    }   
    
    PGresult *res = request(sql, paramValues, paramCount);
    
    if (res == NULL) {
        send(cnx, "Error executing query.\n", 23, 0);
        return;
    }

    int rows = PQntuples(res);
    char json[BUFFER_SIZE] = "[";
    char temp[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < rows; i++) {
        if (i > 0){
            strcat(json, ", ");
        }
        snprintf(temp, BUFFER_SIZE, "{\"id\": %s, \"titre\": \"%s\"}",
                 PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
        strcat(json, temp);
    }
    strcat(json, "]");

    snprintf(buffer, sizeof(buffer), "[LIST_ALL] Result: %s", json);
    output_log(buffer);

    strcat(json, "\n");

    send(cnx, json, strlen(json), 0);
    PQclear(res);
}

#define MAX_ID_LENGTH 49
#define MAX_DATE_LENGTH 10

void get_planning(int cnx, User *usr, const char *buffer) {
    if (!usr->perms.calendrier_disponibilite) {
        send(cnx, "Permission Denied.\n", 20, 0);
        return;
    }

    char id[MAX_ID_LENGTH + 1] = {0};
    char debut[MAX_DATE_LENGTH + 1] = {0};
    char fin[MAX_DATE_LENGTH + 1] = {0};
    char log_msg[BUFFER_SIZE];

    int parsed = sscanf(buffer + 13, "%49s %10s %10s", id, debut, fin);

    if (parsed < 2) {
        send(cnx, "Invalid format. Usage: GET_PLANNING <ID> <DEBUT> [FIN]\n", 60, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] Invalid format !");
        output_log(log_msg);
        return;
    }

    if (strlen(buffer) > strlen("GET_PLANNING") + MAX_ID_LENGTH + MAX_DATE_LENGTH * 2 + 3) {
        send(cnx, "Input too long. Please check your parameters.\n", 47, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] Input too long !");
        output_log(log_msg);
        return;
    }

    if (!validate_date(debut)){
        send(cnx, "Invalid start date foramt. (YYYY-mm-dd)\n", 39, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] Start date (%s) invalid format !", debut);
        output_log(log_msg);
        return;
    }

    if (strlen(fin) > 0 && !validate_date(fin)){
        send(cnx, "Invalid end date foramt. (YYYY-mm-dd)\n", 39, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] End date (%s) invalid format !", fin);
        output_log(log_msg);
        return;
    }

    const char *sql;
    const char *paramValues[3];
    int paramCount;

    if (parsed == 2) {
        sql = "SELECT date_debut, date_fin FROM sae._reservation WHERE id_logement = $1 AND date_fin >= $2 ORDER BY date_debut;";
        paramValues[0] = id;
        paramValues[1] = debut;
        paramCount = 2;
    } else {
        sql = "SELECT date_debut, date_fin FROM sae._reservation WHERE id_logement = $1 AND date_fin >= $2 AND date_debut <= $3 ORDER BY date_debut;";
        paramValues[0] = id;
        paramValues[1] = debut;
        paramValues[2] = fin;
        paramCount = 3;
    }

    PGresult *res = request(sql, paramValues, paramCount);
    
    if (res == NULL) {
        send(cnx, "Error executing query.\n", 23, 0);
        return;
    }

    int rows = PQntuples(res);
    char json[BUFFER_SIZE] = "[";
    char temp[BUFFER_SIZE];

    memset(log_msg, 0, BUFFER_SIZE);
    for (int i = 0; i < rows; i++) {
        if (i > 0) {
            strcat(json, ", ");
        }
        snprintf(temp, BUFFER_SIZE, "{\"debut\": \"%s\", \"fin\": \"%s\"}",
                 PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
        strcat(json, temp);
    }
    strcat(json, "]");

    snprintf(log_msg, sizeof(log_msg), "[GET_AVAILABILITY] Result for logement %s: %s", id, json);
    output_log(log_msg);

    strcat(json, "\n");
    send(cnx, json, strlen(json), 0);
    PQclear(res);
}

void set_availability(int cnx, User *usr, const char *buffer) {
    if (!usr->perms.mise_indispo) {
        send(cnx, "Permission Denied.\n", 20, 0);
        return;
    }

    char id[MAX_ID_LENGTH + 1] = {0};
    char status[2];
    char log_msg[BUFFER_SIZE];

    // Status 0 ou 1
    int parsed = sscanf(buffer + 16, "%49s %1s", id, status);

    if (parsed != 2) {
        send(cnx, "Invalid format. Usage: SET_AVAILABILITY <ID> <0/1>\n", 50, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] Invalid format !");
        output_log(log_msg);
        return;
    }

    if (strlen(buffer) > strlen("set_availability") + MAX_ID_LENGTH + 1) {
        send(cnx, "Input too long. Please check your parameters.\n", 47, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] Input too long !");
        output_log(log_msg);
        return;
    }

    const char *sql;
    const char *paramValues[3];
    int paramCount;

    sql = "UPDATE sae._logement l SET en_ligne = $1 WHERE l.id = $2 AND l.id_proprietaire = $3 RETURNING id, en_ligne;";
    paramValues[0] = status;
    paramValues[1] = id;
    paramValues[2] = usr->id;
    paramCount = 3;

    PGresult *res = request(sql, paramValues, paramCount);
    
    if (res == NULL) {
        send(cnx, "Error executing query.\n", 23, 0);
        return;
    }

    int rows = PQntuples(res);

    if (rows <= 0){
        send(cnx, "ID not found\n", 47, 0);
        snprintf(log_msg, BUFFER_SIZE, "[Argument] Invalid ID (not found for this owner)!");
        output_log(log_msg);
    }

    char json[BUFFER_SIZE] = "[";
    char temp[BUFFER_SIZE];

    memset(log_msg, 0, BUFFER_SIZE);
    for (int i = 0; i < rows; i++) {
        if (i > 0) {
            strcat(json, ", ");
        }
        snprintf(temp, BUFFER_SIZE, "{\"id\": \"%s\", \"status\": \"%s\"}",
                 PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
        strcat(json, temp);
    }
    strcat(json, "]");

    snprintf(log_msg, sizeof(log_msg), "[SET_DISPONIBILITE] Result for logement %s: %s", id, json);
    output_log(log_msg);

    strcat(json, "\n");
    send(cnx, json, strlen(json), 0);
    PQclear(res);
}

User* authenticate(const char* api_key) {
    const char *sql = "SELECT u.id, pseudo, permission FROM sae._api_keys a INNER JOIN sae._utilisateur u ON u.id = a.proprietaire WHERE key = $1;";
    const char *paramValues[1] = {api_key};
    int paramCount = 1;
    PGresult *res = request(sql, paramValues, paramCount);
    if (res == NULL){
        return NULL;
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return NULL;
    }

    User *user = malloc(sizeof(User));
    strncpy(user->id, PQgetvalue(res, 0, 0), sizeof(user->id) - 1);
    user->id[sizeof(user->id) - 1] = '\0';

    strncpy(user->name, PQgetvalue(res, 0, 1), sizeof(user->name) - 1);
    user->name[sizeof(user->name) - 1] = '\0';

    user->perms = extract_permissions(PQgetvalue(res, 0, 2));

    PQclear(res);
    return user;
}

int validate_date(const char* input) {
    struct tm tm;
    // On tente de parse input
    if (strptime(input, "%Y-%m-%d", &tm) == NULL) {
        return 0;
    }
    
    // On vérifie si la date est valide.
    time_t t = mktime(&tm);
    if (t == -1) {
        return 0;
    }
        
    return 1;
}