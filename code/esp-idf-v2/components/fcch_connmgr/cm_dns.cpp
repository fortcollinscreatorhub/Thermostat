// Copyright 2024-2025 Stephen Warren <swarren@wwwdotorg.org>
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <esp_log.h>
#include <esp_netif.h>

#include "fcch_connmgr/cm_net.h"
#include "cm_dns.h"

struct dnspkt_header {
    uint16_t id;

    uint8_t rd : 1;
    uint8_t tc : 1;
    uint8_t aa : 1;
    uint8_t opcode : 4;
    uint8_t qr : 1;

    uint8_t rcode : 4;
    uint8_t z : 3;
    uint8_t ra : 1;

    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static const char *TAG = "cm_dns";
static const size_t dnspkt_impl_len = 512;
static const uint16_t QTYPE_A = 1;
static const uint16_t QCLASS_IN = 1;

static int cm_dns_sock;

static uint8_t *cm_dns_find_questions_end(
    uint8_t *buf,
    size_t buf_len,
    struct dnspkt_header *hdr,
    uint8_t *questions
) {
    ESP_LOGD(TAG, "cm_dns_find_questions_end:");
    uint8_t *buf_after_rd = buf + buf_len;
    uint8_t *read = questions;
    uint16_t qdcount_left = ntohs(hdr->qdcount);
    ESP_LOGD(TAG, "max %d questions to parse", qdcount_left);
    hdr->qdcount = 0;
    for (;;) {
        if (qdcount_left == 0) {
            ESP_LOGD(TAG, "parsed qdcount in header");
            break;
        }
        qdcount_left--;

        uint8_t *question = read;
        ESP_LOGD(TAG, "parse a question at %d", question - buf);

        // QNAME
        uint8_t *label = question;
        uint8_t *after_label = label;
        bool err = false;
        for (;;) {
            uint8_t label_len = *after_label;
            ESP_LOGD(TAG, "label component length %d", label_len);
            after_label += 1 + label_len;
            if (after_label > buf_after_rd) {
                ESP_LOGD(TAG, "label component does not fit buffer");
                err = true;
                break;
            }
            if (label_len == 0) {
                ESP_LOGD(TAG, "no label component");
                break;
            }
        }
        if (err) {
            ESP_LOGD(TAG, "label err");
            break;
        }

        // QTYPE/QCLASS
        uint8_t *after_question = after_label + 4;
        if (after_question > buf_after_rd) {
            ESP_LOGD(TAG, "qtype/qclass does not fit buffer");
            break;
        }
        read = after_question;
        hdr->qdcount++;
    }
    ESP_LOGD(TAG, "%d questions end at offset %d", hdr->qdcount, read - buf);
    hdr->qdcount = htons(hdr->qdcount);
    return read;
}

static size_t cm_dns_gen_answers(
    uint8_t *buf,
    size_t buf_capacity,
    size_t buf_len,
    struct dnspkt_header *hdr,
    uint8_t *questions,
    uint8_t *answers,
    bool *tc
) {
    ESP_LOGD(TAG, "cm_dns_gen_answers:");
    uint8_t *buf_after_rd = answers;
    uint8_t *buf_after_wr = buf + buf_capacity;
    uint8_t *read = questions;
    uint8_t *write = answers;
    uint16_t qdcount_left = ntohs(hdr->qdcount);
    ESP_LOGD(TAG, "max %d questions to respond to", qdcount_left);
    hdr->ancount = 0;
    for (;;) {
        if (qdcount_left == 0) {
            ESP_LOGD(TAG, "parsed qdcount in header");
            break;
        }
        qdcount_left--;

        uint8_t *question = read;
        ESP_LOGD(TAG, "parse a question at %d", question - buf);

        // QNAME
        uint8_t *label = question;
        uint8_t *after_label = label;
        bool err = false;
        for (;;) {
            uint8_t label_len = *after_label;
            ESP_LOGD(TAG, "label component length %d", label_len);
            after_label += 1 + label_len;
            if (after_label > buf_after_rd) {
                ESP_LOGD(TAG, "label component does not fit buffer");
                err = true;
                break;
            }
            if (label_len == 0) {
                ESP_LOGD(TAG, "no label component");
                break;
            }
        }
        if (err) {
            ESP_LOGD(TAG, "label err");
            break;
        }

        // QTYPE/QCLASS
        uint8_t *after_question = after_label + 4;
        if (after_question > buf_after_rd) {
            ESP_LOGD(TAG, "qtype/qclass does not fit buffer");
            break;
        }
        read = after_question;

        uint16_t qtype = ntohs(*(uint16_t *)(after_label + 0));
        if (qtype != QTYPE_A) {
            ESP_LOGD(TAG, "qtype %d != A", (int)qtype);
            continue;
        }
        uint16_t qclass = ntohs(*(uint16_t *)(after_label + 2));
        if (qclass != QCLASS_IN) {
            ESP_LOGD(TAG, "qclass %d != IN", (int)qclass);
            continue;
        }

        size_t label_len = after_label - label;
        // 7 (words) = type + class + ttl(2) + rdlength + rdata(2)
        // 2 == bytes per word
        size_t answer_len = label_len + (7 * 2);

        uint8_t *answer = write;
        uint8_t *after_answer = answer + answer_len;
        if (after_answer > buf_after_wr) {
            ESP_LOGD(TAG, "answer does not fit buffer");
            *tc = true;
            continue;
        }

        ESP_LOGD(TAG, "write an answer at %d", answer - buf);

        esp_netif_ip_info_t info;
        esp_netif_get_ip_info(cm_net_if_ap, &info);

        uint8_t *answer_label = answer;
        memcpy(answer_label, label, label_len);
        uint8_t *after_answer_label = answer_label + label_len;
        *(uint16_t *)(after_answer_label + 0) = htons(QTYPE_A);
        *(uint16_t *)(after_answer_label + 2) = htons(QCLASS_IN);
        *(uint32_t *)(after_answer_label + 4) = htonl(0); // TTL
        *(uint16_t *)(after_answer_label + 8) = htons(4); // RDLENGTH
        *(uint32_t *)(after_answer_label + 10) = info.ip.addr; // RDATA
        write = after_answer;
        hdr->ancount++;
    }
    ESP_LOGD(TAG, "%d answers end at offset %d", hdr->ancount, write - buf);
    hdr->ancount = htons(hdr->ancount);
    return write - buf;
}

static void cm_dns_process_query(
    struct sockaddr_in *client_addr,
    uint8_t *buf,
    size_t buf_capacity,
    size_t buf_len
) {
    bool tc;
    if (buf_len > buf_capacity) {
        ESP_LOGD(TAG, "buf_len > buf_capacity");
        buf_len = buf_capacity;
        tc = true;
    }
    if (buf_len < sizeof(struct dnspkt_header)) {
        ESP_LOGD(TAG, "buf_len < sizeof dnspkt_header");
        return;
    }

    struct dnspkt_header *hdr = (struct dnspkt_header *)buf;
    // Response?
    if (hdr->qr == 1) {
        ESP_LOGD(TAG, "QR == 1");
        return;
    }
    if (hdr->opcode != 0) {
        ESP_LOGD(TAG, "OPCODE != 0");
        return;
    }

    hdr->qr = 1;
    hdr->aa = 0;
    // hdr->tc set below
    hdr->ra = 0;
    hdr->rcode = 0; // else we just don't reply
    //hdr->ancount = 0; // zero'd && incremented by cm_dns_gen_answers
    hdr->nscount = 0;
    hdr->arcount = 0;

    uint8_t *questions = (uint8_t *)&hdr[1];
    uint8_t *answers = cm_dns_find_questions_end(buf, buf_len, hdr, questions);
    buf_len = cm_dns_gen_answers(buf, buf_capacity, buf_len,
        hdr, questions, answers, &tc);

    hdr->tc = !!tc;
    ESP_LOGD(TAG, "DNS: responding");
    sendto(cm_dns_sock, (void *)buf, buf_len, 0,
        (struct sockaddr *)client_addr, sizeof(*client_addr));
}

static void cm_dns_task_init() {
    cm_dns_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (cm_dns_sock == -1) {
        ESP_LOGD(TAG, "socket() -> %d/errno:%d", cm_dns_sock, errno);
        esp_err_t socket_ok = ESP_FAIL;
        ESP_ERROR_CHECK(socket_ok);
    }

    esp_netif_ip_info_t info;
    esp_netif_get_ip_info(cm_net_if_ap, &info);

    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    ESP_LOGI(TAG, "Server address: " IPSTR, IP2STR(&info.ip));
    listen_addr.sin_addr.s_addr = info.ip.addr;
    listen_addr.sin_port = htons(53);
    listen_addr.sin_len = sizeof(listen_addr);
    int ret = bind(cm_dns_sock,
        (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    if (ret != 0) {
        ESP_LOGD(TAG, "bind() -> %d/errno:%d", ret, errno);
        esp_err_t bind_ok = ESP_FAIL;
        ESP_ERROR_CHECK(bind_ok);
    }
}

static void cm_dns_task(void *pvParameters) {
    cm_dns_task_init();
    ESP_LOGI(TAG, "Listening");

    for (;;) {
        uint8_t buf[dnspkt_impl_len];
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        ssize_t buf_len = recvfrom(cm_dns_sock, (void *)buf, sizeof(buf), 0,
            (struct sockaddr *)&client_addr, &client_addr_len);
        ESP_LOGD(TAG, "recvfrom -> %d", buf_len);
        if (buf_len > 0)
            cm_dns_process_query(&client_addr, buf, sizeof(buf), buf_len);
    }
}

void cm_dns_init() {
    xTaskCreate(cm_dns_task, "cm_dns", 4096, NULL, 5, NULL);
}
