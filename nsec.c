/*
 * Part of DNS zone file validator `validns`.
 *
 * Copyright 2011, Anton Berezin <tobez@tobez.org>
 * Modified BSD license.
 * (See LICENSE file in the distribution.)
 *
 */
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "textparse.h"
#include "mempool.h"
#include "carp.h"
#include "rr.h"

static struct rr* nsec_parse(char *name, long ttl, int type, char *s)
{
    struct rr_nsec *rr = getmem(sizeof(*rr));
	struct binary_data bitmap;
	char *str_type = NULL;
	int ltype;

    rr->next_domain = extract_name(&s, "next domain");
	/* TODO: validate next_domain, http://tools.ietf.org/html/rfc4034#section-4.1.1 */

	bitmap = new_set();
	while (s && *s) {
		str_type = extract_label(&s, "type list", "temporary");
		if (!str_type) return NULL;
		ltype = str2rdtype(str_type);
		add_bit_to_set(&bitmap, ltype);
	}
	if (!s)
		return NULL;
	if (!str_type) {
		return bitch("NSEC type list should not be empty");
	}
	rr->type_bitmap = compressed_set(&bitmap);

    return store_record(type, name, ttl, rr);
}

static char* nsec_human(struct rr *rrv)
{
    struct rr_nsec *rr = (struct rr_nsec *)rrv;
    char ss[1024];
	char *s = ss;
	int l;
	char *base;
	int i, k;
	int type;
	char *type_name;

    l = snprintf(s, 1024, "%s", rr->next_domain);
	s += l;
	base = rr->type_bitmap.data;
	while (base - rr->type_bitmap.data < rr->type_bitmap.length) {
		for (i = 0; i < base[1]; i++) {
			for (k = 0; k <= 7; k++) {
				if (base[2+i] & (0x80 >> k)) {
					type = base[0]*256 + i*8 + k;
					type_name = rdtype2str(type);
					l = snprintf(s, 1024-(s-ss), " %s", type_name);
					s += l;
				}
			}
		}
		base += base[1]+2;
	}
    return quickstrdup_temp(ss);
}

static struct binary_data nsec_wirerdata(struct rr *rrv)
{
    struct rr_nsec *rr = (struct rr_nsec *)rrv;

	return compose_binary_data("dd", 1,
		name2wire_name(rr->next_domain), rr->type_bitmap);
}

static void* nsec_validate(struct rr *rrv)
{
    struct rr_nsec *rr = (struct rr_nsec *)rrv;
	int type;
	char *base;
	int i, k;
	struct named_rr *named_rr, *next_named_rr;
	struct rr_set *set;
	uint32_t nsec_distinct_types = 0;
	uint32_t real_distinct_types;

	named_rr = rr->rr.rr_set->named_rr;
	base = rr->type_bitmap.data;
	while (base - rr->type_bitmap.data < rr->type_bitmap.length) {
		for (i = 0; i < base[1]; i++) {
			for (k = 0; k <= 7; k++) {
				if (base[2+i] & (0x80 >> k)) {
					type = base[0]*256 + i*8 + k;
					nsec_distinct_types++;
					set = find_rr_set_in_named_rr(named_rr, type);
					if (!set) {
						return moan(rr->rr.file_name, rr->rr.line, "NSEC mentions %s, but no such record found", rdtype2str(type));
					}
				}
			}
		}
		base += base[1]+2;
	}
	real_distinct_types = get_rr_set_count(named_rr);
	if (real_distinct_types > nsec_distinct_types) {
		return moan(rr->rr.file_name, rr->rr.line, "there are more record types than NSEC mentions");
	}

	next_named_rr = find_next_named_rr(named_rr);
	if (strcmp(rr->next_domain, zone_name) == 0) {
		if (next_named_rr) {
			return moan(rr->rr.file_name, rr->rr.line, "NSEC says %s is the last name, but %s exists",
						named_rr->name, next_named_rr->name);
		}
	} else {
		if (!next_named_rr) {
			return moan(rr->rr.file_name, rr->rr.line, "NSEC says %s comes after %s, but nothing does",
						rr->next_domain, named_rr->name);
		} else if (strcmp(rr->next_domain, next_named_rr->name) != 0) {
			return moan(rr->rr.file_name, rr->rr.line, "NSEC says %s comes after %s, but %s does",
						rr->next_domain, named_rr->name, next_named_rr->name);
		}
	}

	/* TODO: more checks */
	return rr;
}

struct rr_methods nsec_methods = { nsec_parse, nsec_human, nsec_wirerdata, NULL, nsec_validate };
