// Copyright 2017 Altronix Corp.
// This file is part of the tiny-ether library
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
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

#include "ueth.h"
#include "ueth_boot_nodes.h"
#include "usys_io.h"
#include "usys_log.h"
#include "usys_time.h"

int ueth_poll_internal(ueth_context* ctx);
int ueth_on_erro(void* ctx);
int ueth_on_send(void* ctx, int err, const uint8_t* b, uint32_t l);
int ueth_on_recv(void* ctx, int err, uint8_t* b, uint32_t l);

int
ueth_init(ueth_context* ctx, ueth_config* config)
{
    h256 key;

    memset(ctx, 0, sizeof(ueth_context));

    // Copy config.
    ctx->config = *config;

    // Polling mode (p2p enable, etc)
    ctx->poll = ueth_poll_internal;

    if (config->p2p_private_key) {
        rlpx_node_hex_to_bin(config->p2p_private_key, 0, key.b, NULL);
        uecc_key_init_binary(&ctx->id, &key);
    } else {
        uecc_key_init_new(&ctx->id);
    }

    // init constants
    ctx->n = (sizeof(ctx->ch) / sizeof(rlpx_io));

    // Init peer pipes (tcp)
    for (uint32_t i = 0; i < ctx->n; i++) {
        rlpx_io_tcp_init(&ctx->ch[i], &ctx->id, &ctx->config.udp);
        rlpx_io_devp2p_install(&ctx->ch[i]);
    }

    // Init discovery pipe
    rlpx_io_udp_init(&ctx->discovery, &ctx->id, &ctx->config.udp);
    rlpx_io_discovery_install(&ctx->discovery);

    // Setup boot nodes
    ueth_boot(ctx, 4, TEST_NET_6, TEST_NET_15, GETH_P2P_LOCAL, CPP_P2P_LOCAL);

    return 0;
}

void
ueth_deinit(ueth_context* ctx)
{
    // Shutdown any open connections..
    for (uint32_t i = 0; i < ctx->n; i++) rlpx_io_deinit(&ctx->ch[i]);

    // Shutdown udp
    rlpx_io_deinit(&ctx->discovery);

    // Free static key
    uecc_key_deinit(&ctx->id);
}

int
ueth_boot(ueth_context* ctx, int n, ...)
{
    va_list l;
    va_start(l, n);
    const char* enode;
    rlpx_node node;
    if (n > UETH_CONFIG_MAX_BOOTNODES) n = UETH_CONFIG_MAX_BOOTNODES;
    for (uint32_t i = 0; i < (uint32_t)n; i++) {
        enode = va_arg(l, const char*);
        rlpx_node_init_enode(&node, enode);
        rlpx_io_discovery* discovery;
        discovery = rlpx_io_discovery_get_context(&ctx->discovery);
        ctx->bootnodes[i].ip = node.ipv4;
        ctx->bootnodes[i].tcp = node.port_tcp;
        ctx->bootnodes[i].udp = node.port_udp ? node.port_udp : node.port_tcp;
        ktable_insert(
            &discovery->table,
            &node.id,
            node.ipv4,
            node.port_tcp,
            node.port_udp,
            NULL);
        rlpx_node_deinit(&node);
    }
    va_end(l);
    return 0;
}

int
ueth_stop(ueth_context* ctx)
{
    uint32_t mask = 0, i, c = 0, b = 0;
    rlpx_io* ch[ctx->n];
    rlpx_io_devp2p* devp2p;
    for (i = 0; i < ctx->n; i++) {
        if (rlpx_io_is_ready(&ctx->ch[i])) {
            devp2p = ctx->ch[i].protocols[0].context;
            if (!(rlpx_io_devp2p_send_disconnect(
                    devp2p, DEVP2P_DISCONNECT_QUITTING))) {
                mask |= (1 << b);
                ch[b++] = &ctx->ch[i];
            }
        }
    }
    usys_log("[SYS] (shutdown) %s", mask ? "Please be patient..." : "");
    while (mask && ++c < 50) {
        usys_msleep(100);
        rlpx_io_poll(ch, b, 100);
        for (i = 0; i < b; i++) {
            devp2p = ch[i]->protocols[0].context;
            if ((!async_io_has_sock(&devp2p->base->io)) ||
                rlpx_io_is_shutdown(devp2p->base)) {
                mask &= (~(1 << i));
            }
        }
    }
    return 0;
}

int
ueth_poll_internal(ueth_context* ctx)
{
    uint32_t i, b = 0, now = usys_now();
    rlpx_io_discovery* d;
    async_io* ch[ctx->n + 1];

    for (i = 0; i < ctx->n; i++) {
        // Refresh channel if it is in error
        if (rlpx_io_error_get(&ctx->ch[i])) {
            // Kick out of discovery table
            // TODO

            // Refresh space (don't like allocs)
            rlpx_io_refresh(&ctx->ch[i]);
        }
        // Find free node to connect to if empty
        if (!ctx->ch[i].node.port_tcp) {
            // TODO
            // rlpx_io_discovery* d;
            // d = rlpx_io_discovery_get_context(&ctx->discovery);
            // rlpx_io_discovery_connect(d, &ctx->ch[i]);
        }
        // Add to io polling if there is a socket
        if (async_io_has_sock(&ctx->ch[i].io)) {
            ch[b++] = (async_io*)&ctx->ch[i];
        }
    }

    d = rlpx_io_discovery_get_context(&ctx->discovery);
    ktable_poll(&d->table);
    if ((now - ctx->tick) > ctx->config.interval_discovery) {
        ctx->tick = now;
        usys_log(
            "[SYS] want peers (%d/%d)",
            0,
            knodes_size(d->table.nodes, KTABLE_N_NODES));
    }

    // Add our listener to poll
    ch[b++] = (async_io*)&ctx->discovery;
    async_io_poll_n(ch, b, 1);
    return 0;
}

//
//
//
