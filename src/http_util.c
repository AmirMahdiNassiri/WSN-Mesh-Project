#include <stdio.h>
#include <zephyr.h>
#include <string.h>

#include <net/socket.h>
#include <net/net_ip.h>
#include <net/http_client.h>
#include <net/tls_credentials.h>
#include <kernel.h>

#define HTTP_PORT 8000
#define HTTP_PROTOCOL "HTTP/1.2"

#define MAX_RECV_BUF_LEN 512
#define REMOTE_SERVER_ADDRESS  "192.168.10.10"

static uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];

char *http_headers[] = {
    "Content-Length: 30\r\n",
    "Content-Type: application/json\r\n",
    "Authorization: Token 439c3b239670205ec60a0510cabdab6d220da5c2\r\n",
    NULL
};

void error(const char *msg) {
    printk("%s\n", msg);
}

static int setup_socket(sa_family_t family, const char *server, int port, int *sock, 
                        struct sockaddr *addr, socklen_t addr_len) {
    memset(addr, 0, addr_len);

    net_sin(addr)->sin_family = AF_INET;
    net_sin(addr)->sin_port = htons(port);
    inet_pton(family, server, &net_sin(addr)->sin_addr);
    *sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
    return 0;
}

static int payload_callback(int sock, struct http_request *req, void *user_data) {
    (void)send(sock, user_data, strlen(user_data), 0);
    return 0;
}

static void response_callback(struct http_response *resp, enum http_final_call final_data, void *user_data){
    if (final_data == HTTP_DATA_MORE) {
        printk("partial data recieved (%zd bytes)\n", resp->data_len);
    } else if (final_data == HTTP_DATA_FINAL) {
        printk("All data has been received (%zd bytes), status: %s\n", resp->data_len, resp->http_status);
    }

    printk("Response to %s\n", (const char *)user_data);
}

static int connect_socket(sa_family_t family, const char *server, int port, int *sock, struct sockaddr *addr, socklen_t addr_len){
    int res;
    res = setup_socket(family, server, port, sock, addr, addr_len);
    if (res < 0 || *sock < 0) {
        printk("unable to setup socket\n");
        return -1;
    }
    res = connect(*sock, addr, addr_len);
    if (res < 0) {
        printk("can not connect to IPv4 remote (%d)\n", -errno);
        res = -errno;
    }
    return res;
}

int ping_http_server() {
    struct sockaddr_in addr4;
    int sock4 = -1;
    int32_t timeout = 3 * MSEC_PER_SEC;
    int ret;
    int port = HTTP_PORT;

    connect_socket(AF_INET, REMOTE_SERVER_ADDRESS, port, &sock4, (struct sockaddr *)&addr4, sizeof(addr4));

    if(sock4 < 0) {
        printk("cannot create HTTP connection\n");
        return -1;
    }

    struct http_request req;
    memset(&req, 0, sizeof(req));

    req.method = HTTP_GET;
    req.url = "/ping/";
    req.host = REMOTE_SERVER_ADDRESS;
    req.protocol = HTTP_PROTOCOL;
    req.response = response_callback;
    req.recv_buf = recv_buf_ipv4;
    req.recv_buf_len = sizeof(recv_buf_ipv4);
    ret = http_client_req(sock4, &req, timeout, "IPv4 GET");
    close(sock4);
    return ret;
} 

int post_sensor_data(char *data) {
    struct sockaddr_in addr4;
    int sock4 = -1;
    int32_t timeout = 3 * MSEC_PER_SEC;
    int ret;
    int port = HTTP_PORT;

    connect_socket(AF_INET, REMOTE_SERVER_ADDRESS, port, &sock4, (struct sockaddr *)&addr4, sizeof(addr4));

    if(sock4 < 0) {
        printk("cannot create HTTP connection\n");
        return -1;
    }

    struct http_request req;
    memset(&req, 0, sizeof(req));

    struct http_request request;
    memset(&request, 0, sizeof(request));

    request.method = HTTP_POST;
    request.url = "/api/remote/upload/";
    request.host = REMOTE_SERVER_ADDRESS;
    request.protocol = HTTP_PROTOCOL;
    request.content_type_value = "application/json";
    request.payload = data;
    request.payload_len = strlen(data);
    request.response = response_callback;
    request.recv_buf = recv_buf_ipv4;
    request.recv_buf_len = sizeof(recv_buf_ipv4);
    char content_length[30];
    sprintf(content_length, "Content-Length: %d\r\n", strlen(data));
    http_headers[0] = content_length;

    request.header_fields = http_headers;
    ret = http_client_req(sock4, &request, timeout, "IPv4 POST");
    close(sock4);
    return ret;
}
