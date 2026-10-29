#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sqlite3.h>

#pragma comment(lib, "Ws2_32.lib")
#define PORT 8080
#define BUF_SIZE 16384

static sqlite3 *db = NULL;

static void send_response(SOCKET client, int status, const char *status_text,
                          const char *content_type, const char *body) {
    char header[1024];
    int body_len = (int)strlen(body);
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n\r\n",
        status, status_text, content_type, body_len);
    send(client, header, header_len, 0);
    if (body_len > 0) send(client, body, body_len, 0);
}

static void send_json(SOCKET c, int status, const char *status_text, const char *json) {
    send_response(c, status, status_text, "application/json; charset=utf-8", json);
}

static void url_decode(char *s) {
    char *src = s, *dst = s;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) &&
                   isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static int get_form_value(const char *body, const char *key, char *out, size_t out_size) {
    out[0] = '\0';
    size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t part_len = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', part_len);
        if (eq) {
            size_t name_len = (size_t)(eq - p);
            if (name_len == key_len && strncmp(p, key, key_len) == 0) {
                size_t val_len = part_len - name_len - 1;
                if (val_len >= out_size) val_len = out_size - 1;
                memcpy(out, eq + 1, val_len);
                out[val_len] = '\0';
                url_decode(out);
                return 1;
            }
        }
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

static int init_database(void) {
    if (sqlite3_open("data/students.db", &db) != SQLITE_OK) {
        fprintf(stderr, "SQLite open error: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    const char *sql =
        "CREATE TABLE IF NOT EXISTS students ("
        "reg_no TEXT PRIMARY KEY NOT NULL,"
        "name TEXT NOT NULL,"
        "marks REAL NOT NULL CHECK(marks >= 0 AND marks <= 100)"
        ");";
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "SQLite init error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        return 0;
    }
    return 1;
}

static void handle_get_students(SOCKET c) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT reg_no, name, marks FROM students ORDER BY reg_no;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        send_json(c, 500, "Internal Server Error", "{\"error\":\"Database query failed\"}");
        return;
    }

    char json[BUF_SIZE];
    size_t used = 0;
    used += (size_t)snprintf(json + used, sizeof(json) - used, "{\"students\":[");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW && used < sizeof(json) - 256) {
        const unsigned char *reg = sqlite3_column_text(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        double marks = sqlite3_column_double(stmt, 2);
        char safe_reg[256], safe_name[512];
        snprintf(safe_reg, sizeof(safe_reg), "%s", reg ? (const char *)reg : "");
        snprintf(safe_name, sizeof(safe_name), "%s", name ? (const char *)name : "");
        /* Escape quotes and backslashes for JSON; reject/control other characters simply. */
        char er[512], en[1024];
        size_t a = 0, b = 0;
        for (size_t i = 0; safe_reg[i] && a < sizeof(er)-2; i++) {
            if (safe_reg[i] == '"' || safe_reg[i] == '\\') er[a++] = '\\';
            if ((unsigned char)safe_reg[i] >= 32) er[a++] = safe_reg[i];
        }
        er[a] = 0;
        for (size_t i = 0; safe_name[i] && b < sizeof(en)-2; i++) {
            if (safe_name[i] == '"' || safe_name[i] == '\\') en[b++] = '\\';
            if ((unsigned char)safe_name[i] >= 32) en[b++] = safe_name[i];
        }
        en[b] = 0;
        used += (size_t)snprintf(json + used, sizeof(json) - used,
            "%s{\"reg_no\":\"%s\",\"name\":\"%s\",\"marks\":%.2f}",
            first ? "" : ",", er, en, marks);
        first = 0;
    }
    snprintf(json + used, sizeof(json) - used, "]}");
    sqlite3_finalize(stmt);
    send_json(c, 200, "OK", json);
}

static void handle_add_student(SOCKET c, const char *body) {
    char reg[128], name[256], marks_text[64];
    if (!get_form_value(body, "reg_no", reg, sizeof(reg)) ||
        !get_form_value(body, "name", name, sizeof(name)) ||
        !get_form_value(body, "marks", marks_text, sizeof(marks_text))) {
        send_json(c, 400, "Bad Request", "{\"error\":\"Registration number, name and marks are required\"}");
        return;
    }
    if (!reg[0] || !name[0]) {
        send_json(c, 400, "Bad Request", "{\"error\":\"Registration number and name cannot be empty\"}");
        return;
    }
    char *end = NULL;
    double marks = strtod(marks_text, &end);
    if (end == marks_text || *end != '\0' || marks < 0 || marks > 100) {
        send_json(c, 400, "Bad Request", "{\"error\":\"Marks must be a number from 0 to 100\"}");
        return;
    }

    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO students(reg_no, name, marks) VALUES(?,?,?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        send_json(c, 500, "Internal Server Error", "{\"error\":\"Could not prepare database insert\"}");
        return;
    }
    sqlite3_bind_text(stmt, 1, reg, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, marks);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        if (strstr(sqlite3_errmsg(db), "UNIQUE") || strstr(sqlite3_errmsg(db), "PRIMARY KEY")) {
            send_json(c, 409, "Conflict", "{\"error\":\"A student with this registration number already exists\"}");
        } else {
            send_json(c, 500, "Internal Server Error", "{\"error\":\"Database insert failed\"}");
        }
        return;
    }
    send_json(c, 201, "Created", "{\"message\":\"Student added successfully\"}");
}

static void handle_delete_student(SOCKET c, const char *path) {
    const char *reg = path + strlen("/api/students/");
    if (!*reg) {
        send_json(c, 400, "Bad Request", "{\"error\":\"Registration number is required\"}");
        return;
    }
    char decoded[256];
    snprintf(decoded, sizeof(decoded), "%s", reg);
    url_decode(decoded);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "DELETE FROM students WHERE reg_no = ?;", -1, &stmt, NULL) != SQLITE_OK) {
        send_json(c, 500, "Internal Server Error", "{\"error\":\"Database delete failed\"}");
        return;
    }
    sqlite3_bind_text(stmt, 1, decoded, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    if (!changed) {
        send_json(c, 404, "Not Found", "{\"error\":\"Student not found\"}");
        return;
    }
    send_json(c, 200, "OK", "{\"message\":\"Student deleted successfully\"}");
}

static int read_file(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return 1;
}

static void handle_client(SOCKET c) {
    char request[BUF_SIZE];
    int n = recv(c, request, sizeof(request) - 1, 0);
    if (n <= 0) return;
    request[n] = '\0';

    char method[16] = {0}, path[512] = {0};
    if (sscanf(request, "%15s %511s", method, path) != 2) {
        send_json(c, 400, "Bad Request", "{\"error\":\"Invalid HTTP request\"}");
        return;
    }

    char *body = strstr(request, "\r\n\r\n");
    if (body) body += 4; else body = "";

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/students") == 0) {
        handle_get_students(c);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/students") == 0) {
        handle_add_student(c, body);
    } else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/api/students/", 14) == 0) {
        handle_delete_student(c, path);
    } else if (strcmp(method, "GET") == 0 &&
               (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        char file[BUF_SIZE];
        if (read_file("public/index.html", file, sizeof(file)))
            send_response(c, 200, "OK", "text/html; charset=utf-8", file);
        else send_response(c, 404, "Not Found", "text/plain", "Missing public/index.html");
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/style.css") == 0) {
        char file[BUF_SIZE];
        if (read_file("public/style.css", file, sizeof(file)))
            send_response(c, 200, "OK", "text/css; charset=utf-8", file);
        else send_response(c, 404, "Not Found", "text/plain", "Missing public/style.css");
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/app.js") == 0) {
        char file[BUF_SIZE];
        if (read_file("public/app.js", file, sizeof(file)))
            send_response(c, 200, "OK", "application/javascript; charset=utf-8", file);
        else send_response(c, 404, "Not Found", "text/plain", "Missing public/app.js");
    } else {
        send_json(c, 404, "Not Found", "{\"error\":\"Route not found\"}");
    }
}

int main(void) {
    WSADATA wsa;
    if (!init_database()) return 1;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "Could not initialize Winsock.\n");
        sqlite3_close(db);
        return 1;
    }
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "Could not create socket.\n");
        WSACleanup();
        sqlite3_close(db);
        return 1;
    }
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(server, 10) == SOCKET_ERROR) {
        fprintf(stderr, "Could not bind to port %d. Is another server using it?\n", PORT);
        closesocket(server);
        WSACleanup();
        sqlite3_close(db);
        return 1;
    }
    printf("Student Record Management server is running.\n");
    printf("Open http://localhost:%d in your browser.\n", PORT);
    printf("Press Ctrl+C to stop the server.\n");
    while (1) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        handle_client(client);
        shutdown(client, SD_SEND);
        closesocket(client);
    }
    closesocket(server);
    WSACleanup();
    sqlite3_close(db);
    return 0;
}
