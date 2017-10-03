#include "rlpx_frame.h"

int frame_egress(rlpx* s,
                 const uint8_t* x,
                 size_t xlen,
                 uint8_t* out,
                 uint8_t* mac);

int frame_ingress(rlpx* s,
                  const uint8_t* x,
                  size_t xlen,
                  const uint8_t* expect,
                  uint8_t* out);
int
rlpx_frame_write(rlpx* s,
                 uint32_t type,
                 uint32_t id,
                 urlp** body_p,
                 uint8_t* out,
                 size_t* l)
{
    int err = 0;
    uint32_t sz = urlp_print_size(*body_p);
    uint8_t body[sz];
    urlp_print(*body_p, body, sz);
    err = rlpx_frame_write_rlp(s, type, id, body, sz, out, l);
    urlp_free(body_p);
    return err;
}

int
rlpx_frame_write_rlp(rlpx* s,
                     uint32_t type,
                     uint32_t id,
                     uint8_t* rlp,
                     size_t rlplen,
                     uint8_t* out,
                     size_t* l)
{
    size_t totlen = AES_LEN(rlplen);
    uint8_t head[32];
    if (*l < (32 + totlen + 16)) {
        *l = 32 + totlen + 16;
        return -1;
    }
    *l = 32 + totlen + 16;
    memset(head, 0, 32);
    WRITE_BE(3, head, (uint8_t*)&totlen);
    head[3] = '\xc2', head[4] = '\x80' + type, head[5] = '\x80' + id;
    frame_egress(s, head, 16, out, &out[16]);
    // frame_egress(s, rlp, rlplen, &out[32], &out[32 + totlen]);
    return 0;
}

int
rlpx_frame_parse(rlpx* s, const uint8_t* frame, size_t l, urlp** rlp_p)
{

    int err = 0;
    uint32_t sz;
    urlp *head = NULL, *body = NULL;

    if (l < 32) return -1;

    // Parse header
    err = rlpx_frame_parse_header(s, frame, &head, &sz);
    if (err) return err;

    // Check length (accounts for aes padding)
    if (l < (32 + (AES_LEN(sz)) + 16)) {
        urlp_free(&head);
        return err;
    }

    // Parse body
    err = rlpx_frame_parse_body(s, frame + 32, sz, &body);
    if (err) {
        urlp_free(&head);
        return err;
    }

    // Return rlp frame to caller
    if (!*rlp_p) {
        *rlp_p = urlp_list();
        if (!*rlp_p) {
            urlp_free(&head);
            urlp_free(&body);
            return -1;
        }
    }
    urlp_push(*rlp_p, head);
    urlp_push(*rlp_p, body);
    return err;
}

int
rlpx_frame_parse_header(rlpx* s,
                        const uint8_t* header,
                        urlp** header_urlp,
                        uint32_t* body_len)
{
    int err = -1;
    uint8_t temp[32];

    err = frame_ingress(s, header, 16, &header[16], temp);
    if (err) return err;

    // Read in big endian length prefix, give to caller
    *body_len = 0;
    READ_BE(3, body_len, temp);

    // Parse header rlp, give to caller
    *header_urlp = urlp_parse(temp + 3, 13);
    return *header_urlp ? 0 : -1;
}

int
rlpx_frame_parse_body(rlpx* s, const uint8_t* frame, uint32_t l, urlp** rlp)
{
    int err;
    // uint8_t temp[32];
    uint8_t body[(l = l % 16 ? AES_LEN(l) : l)];
    // ukeccak256_update(&s->imac, (uint8_t*)frame, l);
    // ukeccak256_digest(&s->imac, temp);
    err = frame_ingress(s, frame, l, frame + l, body);
    if (err) return err;

    if (body[0] < 0xc0) {
        // Some technical debt? Early packets did not nest their body frames
        // So we nest them here and pass up stack and we'll see how that goes
        *rlp = urlp_list();
        if (rlp) {
            urlp_push(*rlp, urlp_parse(body, 1));         // packet type
            urlp_push(*rlp, urlp_parse(body + 1, l - 1)); // packet data
        }
    } else {
        // This packet appears to be proper
        *rlp = urlp_parse(body, l);
    }
    return *rlp ? 0 : -1;
}

int
frame_egress(rlpx* s, const uint8_t* x, size_t xlen, uint8_t* out, uint8_t* mac)
{
    // encrypt
    // HEADER:
    // egress.update(aes(mac-secret,egress-mac) ^ header-ciphertext).digest
    //
    // FRAME:
    // egress-mac.update(aes(mac-secret,egress-mac) ^
    // left128(egress-mac.update(frame-ciphertext).digest))
    uint8_t xin[32];
    memset(xin, 0, 32);
    if (uaes_crypt_ctr_update(&s->aes_enc, x, xlen, out)) return -1;
    if (xlen > 16) {
        ukeccak256_update(&s->emac, (uint8_t*)out, xlen);
        ukeccak256_digest(&s->emac, xin);
    } else {
        memcpy(xin, x, xlen);
    }
    ukeccak256_update(&s->emac, NULL, 0);
    ukeccak256_digest(&s->emac, mac);          // mac-secret
    uaes_crypt_ecb_enc(&s->aes_mac, mac, mac); // aes(mac-secret)
    XORN(mac, xin, 16);                        // aes(mac-secret)^header-cipher
    ukeccak256_update(&s->emac, mac, 16);      // ingress(...)
    ukeccak256_digest(&s->emac, mac);          // ingress(...).digest
    return 0;
}

int
frame_ingress(rlpx* s,
              const uint8_t* x,
              size_t xlen,
              const uint8_t* expect,
              uint8_t* out)
{
    // Check mac and decrypt
    // HEADER:
    // ingress.update(aes(mac-secret,ingress-mac) ^ header-ciphertext).digest
    //
    // FRAME: (ingress frame ciphertext before calling))
    // ingres-mac.update(aes(mac-secret,ingres-mac) ^
    // left128(ingres-mac.update(frame-ciphertext).digest))
    uint8_t tmp[32], xin[32];
    memset(xin, 0, 32);
    if (xlen > 16) {
        ukeccak256_update(&s->imac, (uint8_t*)x, xlen);
        ukeccak256_digest(&s->imac, xin);
    } else {
        memcpy(xin, x, xlen);
    }
    ukeccak256_update(&s->imac, NULL, 0);
    ukeccak256_digest(&s->imac, tmp);          // mac-secret
    uaes_crypt_ecb_enc(&s->aes_mac, tmp, tmp); // aes_mac(secret)
    XORN(tmp, xin, 16);                        // aes_mac(secret)^header-cipher
    ukeccak256_update(&s->imac, tmp, 16);      // ingress(...)
    ukeccak256_digest(&s->imac, tmp);          // ingress(...).digest
    if (memcmp(tmp, expect, 16)) return -1;    // compare expect with actual
    if (uaes_crypt_ctr_update(&s->aes_dec, x, xlen, out)) return -1;
    return 0;
}

//
//
//
