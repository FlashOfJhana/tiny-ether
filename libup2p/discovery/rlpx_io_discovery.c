// Copyright 2017 Altronix Corp.
// This file is part of the tiny-ether library
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be state,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

/**
 * @author Thomas Chiantia <thomas@altronix>
 * @date 2017
 */

#include "rlpx_io_discovery.h"
#include "ukeccak256.h"
#include "urand.h"
#include "usys_log.h"
#include "usys_time.h"

int rlpx_io_discovery_table_ping(ktable* t, knodes* n);
int rlpx_io_discovery_table_find(ktable* t, knodes* n, uint8_t* b, uint32_t l);

ktable_settings g_rlpx_io_discovery_table_settings = {
    .refresh = 6000,
    .alpha = 3,
    .pong_timeout = 8000,
    .want_ping = rlpx_io_discovery_table_ping,
    .want_find = rlpx_io_discovery_table_find
};

void
rlpx_io_discovery_init(rlpx_io_discovery* self, rlpx_io* base)
{
    memset(self, 0, sizeof(rlpx_io_discovery));

    //
    self->base = base;

    //
    base->protocols[0].context = self;
    base->protocols[0].ready = rlpx_io_discovery_ready;
    base->protocols[0].recv = rlpx_io_discovery_recv;
    base->protocols[0].uninstall = rlpx_io_discovery_uninstall;

    //
    ktable_init(&self->table, &g_rlpx_io_discovery_table_settings, self);
}

int
rlpx_io_discovery_install(rlpx_io* base)
{
    rlpx_io_discovery* self = base->protocols[0].context
                                  ? NULL
                                  : rlpx_malloc(sizeof(rlpx_io_discovery));
    if (self) {
        rlpx_io_discovery_init(self, base);
        return 0;
    }
    return -1;
}

void
rlpx_io_discovery_uninstall(void** ptr_p)
{
    rlpx_io_discovery* ptr = *ptr_p;
    *ptr_p = NULL;
    ktable_deinit(&ptr->table);
    rlpx_free(ptr);
}

rlpx_io_discovery*
rlpx_io_discovery_get_context(rlpx_io* rlpx)
{
    return rlpx->protocols[0].context;
}

int
rlpx_io_discovery_connect(rlpx_io_discovery* self, rlpx_io* ch)
{
    int err = -1;
    return err;
    // for (uint32_t i = 0; i < KTABLE_SIZE; i++) {
    // if (self->table.nodes[i].state == KNODE_STATE_PENDING) {
    //    err = rlpx_io_connect(
    //        ch,
    //        &self->table.nodes[i].nodeid,
    //        self->table.nodes[i].ip,
    //        self->table.nodes[i].tcp);
    //    if (err) {
    //        self->table.nodes[i].state = KNODE_STATE_FREE;
    //    } else {
    //        self->table.nodes[i].state = KNODE_STATE_CONNECTING;
    //        break;
    //    }
    //}
    //}
    return err;
}

int
rlpx_io_discovery_ready(void* self)
{
    ((void)self);
    return 0;
}

int
rlpx_io_discovery_recv(void* ctx, const urlp* rlp)
{
    rlpx_io_discovery* self = ctx;
    RLPX_DISCOVERY type = -1;
    uint16_t t;
    knodes src, dst;
    uecc_public_key target;
    uint32_t tmp; // timestamp or ipv4
    uint8_t buff32[32];
    int err = -1;
    const urlp* crlp = urlp_at(rlp, 1);
    if (!crlp) return -1;
    if (urlp_idx_to_u16(rlp, 0, &t)) return -1;
    type = (RLPX_DISCOVERY)t;

    memset(&src, 0, sizeof(knodes));
    memset(&dst, 0, sizeof(knodes));
    memset(buff32, 0, sizeof(buff32));

    if (type == RLPX_DISCOVERY_PING) {

        err = rlpx_io_discovery_recv_ping(&crlp, buff32, &src, &dst, &tmp);
        if (!err) {

            // Received a ping packet
            // send a pong on device io...
            // If not in table
            // 	- Add node into table
            // 	- Send ping
            // If in table
            // 	- Update UDP from socket data
            ktable_on_ping(
                &self->table,
                &self->base->node.id,
                src.ip,
                src.tcp,
                async_io_port(&self->base->io));
            usys_log("[ IN] [UDP] (ping) %s:%d", usys_htoa(src.ip), src.udp);
        }
    } else if (type == RLPX_DISCOVERY_PONG) {

        err = rlpx_io_discovery_recv_pong(&crlp, &dst, buff32, &tmp);
        if (!err) {

            // Received a pong packet
            // If not in table
            //  - Unsolicited pong rejected
            // If in table
            //  - Clear timeout
            //  - Update ip address
            ktable_on_pong(
                &self->table,
                &self->base->node.id,
                async_io_ip_addr(&self->base->io),
                0,
                0);
            usys_log(
                "[ IN] [UDP] (pong) %s:%d",
                usys_htoa(async_io_ip_addr(&self->base->io)),
                async_io_port(&self->base->io));
        }
    } else if (type == RLPX_DISCOVERY_FIND) {

        err = rlpx_io_discovery_recv_find(&crlp, &target, &tmp);
        if (!err) {

            // Received request for our neighbours.
            // We send empty neighbours since we are not kademlia
            // We are leech looking for light clients servers
            return rlpx_io_discovery_send_neighbours(
                self,
                async_io_ip_addr(&self->base->io),
                async_io_port(&self->base->io),
                &self->table,
                usys_now() + 15);
        }
    } else if (type == RLPX_DISCOVERY_NEIGHBOURS) {

        // Received some neighbours
        err = ktable_on_neighbours(&self->table, &crlp);
        usys_log(
            "[ IN] [UDP] (neighbours) %s:%d",
            usys_htoa(async_io_ip_addr(&self->base->io)),
            async_io_port(&self->base->io));
    } else {
        // error
    }

    return err;
}

int
rlpx_io_discovery_recv_ping(
    const urlp** rlp,
    uint8_t* version32,
    knodes* src,
    knodes* dst,
    uint32_t* timestamp)
{
    int err;
    uint32_t sz = 32, n = urlp_children(*rlp);
    if (n < 4) return -1;
    if ((!(err = urlp_idx_to_mem(*rlp, 0, version32, &sz))) && //
        (!(err = knodes_rlp_to_node(urlp_at(*rlp, 1), src))) &&
        (!(err = knodes_rlp_to_node(urlp_at(*rlp, 2), dst))) &&
        (!(err = urlp_idx_to_u32(*rlp, 3, timestamp)))) {
        return err;
    }
    return err;
}

int
rlpx_io_discovery_recv_pong(
    const urlp** rlp,
    knodes* to,
    uint8_t* echo32,
    uint32_t* timestamp)
{
    int err;
    uint32_t sz = 32, n = urlp_children(*rlp);
    if (n < 3) return -1;
    if ((!(err = knodes_rlp_to_node(urlp_at(*rlp, 0), to))) &&
        (!(err = urlp_idx_to_mem(*rlp, 1, echo32, &sz))) &&
        (!(err = urlp_idx_to_u32(*rlp, 2, timestamp)))) {
        return err;
    }
    return err;
}

int
rlpx_io_discovery_recv_find(const urlp** rlp, uecc_public_key* q, uint32_t* ts)
{
    int err = -1;
    uint32_t publen = 64, n = urlp_children(*rlp);
    uint8_t pub[65] = { 0x04 };
    if (n < 2) return err;
    if ((!(err = urlp_idx_to_mem(*rlp, 0, &pub[1], &publen))) &&
        //(!(err = uecc_btoq(pub, publen + 1, q))) && // TODO weird vals here
        (!(err = urlp_idx_to_u32(*rlp, 1, ts)))) {
        // TODO io errors
        // write tests to mimick send/recv
        // make all io writes sync or use queue
        // Trace mem bug
        // uint32_t l = 133;
        // char hex[l];
        // rlpx_node_bin_to_hex(pub, 65, hex, &l);
        // q = NULL;
        err = 0;
    }
    return err;
}

int
rlpx_io_discovery_write(
    uecc_ctx* key,
    RLPX_DISCOVERY type,
    const urlp* rlp,
    uint8_t* b,
    uint32_t* l)
{
    int err = -1;
    uecc_signature sig;
    h256 shash;
    uint32_t sz = *l - (32 + 65 + 1);

    // || packet-data
    err = urlp_print(rlp, &b[32 + 65 + 1], &sz);
    if (!err) {

        // || packet-type || packet-data
        b[32 + 65] = type;

        // sign(sha3(packet-type || packet-data)) || packet-type || packet-data
        ukeccak256((uint8_t*)&b[32 + 65], sz + 1, shash.b, 32);
        uecc_sign(key, shash.b, 32, &sig);
        uecc_sig_to_bin(&sig, &b[32]);

        // hash || sig || packet-type || packet-data
        ukeccak256((uint8_t*)&b[32], sz + 1 + 65, b, 32);

        *l = 32 + 65 + 1 + sz;
    }
    return err;
}
int
rlpx_io_discovery_write_ping(
    uecc_ctx* skey,
    uint32_t ver,
    const knodes* ep_src,
    const knodes* ep_dst,
    uint32_t timestamp,
    uint8_t* dst,
    uint32_t* l)
{
    int err = -1;
    urlp* rlp = urlp_list();
    if (rlp) {
        urlp_push_u32(rlp, ver);
        urlp_push(rlp, knodes_node_to_rlp(ep_src));
        urlp_push(rlp, knodes_node_to_rlp(ep_dst));
        urlp_push_u32(rlp, timestamp);

        // TODO the first 32 bytes of the udp packet is used as an echo.
        // This can track the echo's from pongs
        err = rlpx_io_discovery_write(skey, RLPX_DISCOVERY_PING, rlp, dst, l);
        urlp_free(&rlp);
    }
    return err;
}

int
rlpx_io_discovery_write_pong(
    uecc_ctx* skey,
    const knodes* ep_to,
    h256* echo,
    uint32_t timestamp,
    uint8_t* dst,
    uint32_t* l)
{
    int err = -1;
    urlp* rlp = urlp_list();
    if (rlp) {
        urlp_push(rlp, knodes_node_to_rlp(ep_to));
        urlp_push_u8_arr(rlp, echo->b, 32);
        urlp_push_u32(rlp, timestamp);
        err = rlpx_io_discovery_write(skey, RLPX_DISCOVERY_PONG, rlp, dst, l);
        urlp_free(&rlp);
    }
    return err;
}

int
rlpx_io_discovery_write_find(
    uecc_ctx* skey,
    uecc_public_key* nodeid,
    uint32_t timestamp,
    uint8_t* b,
    uint32_t* l)
{
    int err = -1;
    urlp* rlp = urlp_list();
    uint8_t pub[65] = { 0x04 };
    if (nodeid) {
        uecc_qtob(nodeid, pub, sizeof(pub));
    } else {
        urand(&pub[1], 64);
    }
    if (rlp) {
        urlp_push_u8_arr(rlp, &pub[1], 64);
        urlp_push_u32(rlp, timestamp);
        err = rlpx_io_discovery_write(skey, RLPX_DISCOVERY_FIND, rlp, b, l);
        urlp_free(&rlp);
    }
    return err;
}

int
rlpx_io_discovery_write_neighbours(
    uecc_ctx* skey,
    ktable* table,
    uint32_t timestamp,
    uint8_t* b,
    uint32_t* l)
{
    ((void)table); // we don't send anything
    int err = -1;
    urlp* rlp = urlp_list();
    if (rlp) {
        urlp_push(rlp, urlp_list()); // empty neighbours!
        urlp_push_u32(rlp, timestamp);
        err =
            rlpx_io_discovery_write(skey, RLPX_DISCOVERY_NEIGHBOURS, rlp, b, l);
        urlp_free(&rlp);
    }
    return err;
}

int
rlpx_io_discovery_send_ping(
    rlpx_io_discovery* self,
    uint32_t ip,
    uint32_t port,
    const knodes* ep_src,
    const knodes* ep_dst,
    uint32_t timestamp)
{
    int err;
    uint32_t len = 200;
    uint8_t stack[len];
    err = rlpx_io_discovery_write_ping(
        self->base->skey,
        4,
        ep_src,
        ep_dst,
        timestamp ? timestamp : usys_now() + 15,
        stack,
        &len);
    // usys_log("[OUT] [UDP] (ping) (size: %d) %s", len, usys_htoa(ip));
    if (!err) err = rlpx_io_sendto(self->base, ip, port, stack, len);
    return err;
}

int
rlpx_io_discovery_send_pong(
    rlpx_io_discovery* self,
    uint32_t ip,
    uint32_t port,
    const knodes* ep_to,
    h256* echo,
    uint32_t timestamp)
{
    int err;
    uint32_t len = 200;
    uint8_t stack[len];
    err = rlpx_io_discovery_write_pong(
        self->base->skey,
        ep_to,
        echo,
        timestamp ? timestamp : usys_now() + 15,
        stack,
        &len);
    if (!err) err = rlpx_io_sendto(self->base, ip, port, stack, len);
    return err;
}

int
rlpx_io_discovery_send_find(
    rlpx_io_discovery* self,
    uint32_t ip,
    uint32_t port,
    uecc_public_key* nodeid,
    uint32_t timestamp)
{
    int err;
    uint32_t len = 200;
    uint8_t stack[len];
    err = rlpx_io_discovery_write_find(
        self->base->skey,
        nodeid,
        timestamp ? timestamp : usys_now() + 15,
        stack,
        &len);
    // usys_log("[OUT] [UDP] (find) %s", usys_htoa(ip));
    if (!err) err = rlpx_io_sendto(self->base, ip, port, stack, len);
    return err;
}

int
rlpx_io_discovery_send_neighbours(
    rlpx_io_discovery* self,
    uint32_t ip,
    uint32_t port,
    ktable* table,
    uint32_t timestamp)
{
    int err;
    uint32_t len = 200;
    uint8_t stack[len];
    err = rlpx_io_discovery_write_neighbours(
        self->base->skey,
        table,
        timestamp ? timestamp : usys_now() + 15,
        stack,
        &len);
    if (!err) err = rlpx_io_sendto(self->base, ip, port, stack, len);
    return err;
}

int
rlpx_io_discovery_table_ping(ktable* t, knodes* n)
{

    int err;
    rlpx_io_discovery* self = t->context;
    knodes src = {.tcp = *self->base->listen_port,
                  .udp = *self->base->listen_port,
                  .ip = 0,
                  .nodeid = self->base->node.id };
    err = rlpx_io_discovery_send_ping(
        self, //
        n->ip,
        n->udp,
        &src,
        n,
        usys_now() + 15);
    return err;
}

int
rlpx_io_discovery_table_find(ktable* t, knodes* n, uint8_t* b, uint32_t l)
{
    rlpx_io_discovery* self = t->context;
    return rlpx_io_discovery_send_find(
        self, n->ip, n->udp, NULL, usys_now() + 15);
}
