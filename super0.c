/*
 * mdadm - manage Linux "md" devices aka RAID arrays.
 *
 * Copyright (C) 2001-2009 Neil Brown <neilb@suse.de>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    Author: Neil Brown
 *    Email: <neilb@suse.de>
 */

#define HAVE_STDINT_H 1
#include "mdadm.h"
#include "sha1.h"
/*
 * All handling for the 0.90.0 version superblock is in
 * this file.
 * This includes:
 *   - finding, loading, and writing the superblock.
 *   - initialising a new superblock
 *   - printing the superblock for --examine
 *   - printing part of the superblock for --detail
 * .. other stuff
 */


static unsigned long calc_sb0_csum(mdp_super_t *super)
{
	unsigned long csum = super->sb_csum;
	unsigned long newcsum;
	super->sb_csum= 0 ;
	newcsum = calc_csum(super, MD_SB_BYTES);
	super->sb_csum = csum;
	return newcsum;
}


static void super0_swap_endian(struct mdp_superblock_s *sb)
{
	/* as super0 superblocks are host-endian, it is sometimes
	 * useful to be able to swap the endianness
	 * as (almost) everything is u32's we byte-swap every 4byte
	 * number.
	 * We then also have to swap the events_hi and events_lo
	 */
	char *sbc = (char *)sb;
	__u32 t32;
	int i;

	for (i=0; i < MD_SB_BYTES ; i+=4) {
		char t = sbc[i];
		sbc[i] = sbc[i+3];
		sbc[i+3] = t;
		t=sbc[i+1];
		sbc[i+1]=sbc[i+2];
		sbc[i+2]=t;
	}
	t32 = sb->events_hi;
	sb->events_hi = sb->events_lo;
	sb->events_lo = t32;

	t32 = sb->cp_events_hi;
	sb->cp_events_hi = sb->cp_events_lo;
	sb->cp_events_lo = t32;

}

#ifndef MDASSEMBLE

static void examine_super0(struct supertype *st, char *homehost)
{
	mdp_super_t *sb = st->sb;
	time_t atime;
	int d;
	int delta_extra = 0;
	char *c;

	printf("          Magic : %08x\n", sb->md_magic);
	printf("        Version : %d.%02d.%02d\n", sb->major_version, sb->minor_version,
	       sb->patch_version);
	if (sb->minor_version >= 90) {
		printf("           UUID : %08x:%08x:%08x:%08x", sb->set_uuid0, sb->set_uuid1,
		       sb->set_uuid2, sb->set_uuid3);
		if (homehost) {
			char buf[20];
			void *hash = sha1_buffer(homehost,
						 strlen(homehost),
						 buf);
			if (memcmp(&sb->set_uuid2, hash, 8)==0)
				printf(" (local to host %s)", homehost);
		}
		printf("\n");
	} else
		printf("           UUID : %08x\n", sb->set_uuid0);

	if (sb->not_persistent)
		printf("           Eedk : not persistent\n");

	atime = sb->ctime;
	printf("  Creation Time : %.24s\n", ctime(&atime));
	c=map_num(pers, sb->level);
	printf("     Raid Level : %s\n", c?c:"-unknown-");
	if ((int)sb->level >= 0) {
		int ddsks=0;
		printf("  Used Dev Size : %d%s\n", sb->size,
		       human_size((long long)sb->size<<10));
		switch(sb->level) {
		case 1: ddsks=1;break;
		case 4:
		case 5: ddsks = sb->raid_disks-1; break;
		case 6: ddsks = sb->raid_disks-2; break;
		case 10: ddsks = sb->raid_disks / (sb->layout&255) / ((sb->layout>>8)&255);
		}
		if (ddsks)
			printf("     Array Size : %llu%s\n", (unsigned long long)ddsks * sb->size,
			       human_size(ddsks*(long long)sb->size<<10));
	}
	printf("   Raid Devices : %d\n", sb->raid_disks);
	printf("  Total Devices : %d\n", sb->nr_disks);
	printf("Preferred Minor : %d\n", sb->md_minor);
	printf("\n");
	if (sb->minor_version > 90 && (sb->reshape_position+1) != 0) {
		printf("  Reshape pos'n : %llu%s\n", (unsigned long long)sb->reshape_position/2, human_size((long long)sb->reshape_position<<9));
		if (sb->delta_disks) {
			printf("  Delta Devices : %d", sb->delta_disks);
			printf(" (%d->%d)\n", sb->raid_disks-sb->delta_disks, sb->raid_disks);
			if (((int)sb->delta_disks) < 0)
				delta_extra = - sb->delta_disks;
		}
		if (sb->new_level != sb->level) {
			c = map_num(pers, sb->new_level);
			printf("      New Level : %s\n", c?c:"-unknown-");
		}
		if (sb->new_layout != sb->layout) {
			if (sb->level == 5) {
				c = map_num(r5layout, sb->new_layout);
				printf("     New Layout : %s\n", c?c:"-unknown-");
			}
			if (sb->level == 6) {
				c = map_num(r6layout, sb->new_layout);
				printf("     New Layout : %s\n", c?c:"-unknown-");
			}
			if (sb->level == 10) {
				printf("     New Layout : near=%d, %s=%d\n",
				       sb->new_layout&255,
				       (sb->new_layout&0x10000)?"offset":"far",
				       (sb->new_layout>>8)&255);
			}
		}
		if (sb->new_chunk != sb->chunk_size)
			printf("  New Chunksize : %d\n", sb->new_chunk);
		printf("\n");
	}
	atime = sb->utime;
	printf("    Update Time : %.24s\n", ctime(&atime));
	printf("          State : %s\n",
	       (sb->state&(1<<MD_SB_CLEAN))?"clean":"active");
	if (sb->state & (1<<MD_SB_BITMAP_PRESENT))
		printf("Internal Bitmap : present\n");
	printf(" Active Devices : %d\n", sb->active_disks);
	printf("Working Devices : %d\n", sb->working_disks);
	printf(" Failed Devices : %d\n", sb->failed_disks);
	printf("  Spare Devices : %d\n", sb->spare_disks);
	if (calc_sb0_csum(sb) == sb->sb_csum)
		printf("       Checksum : %x - correct\n", sb->sb_csum);
	else
		printf("       Checksum : %x - expected %lx\n", sb->sb_csum, calc_sb0_csum(sb));
	printf("         Events : %llu\n",
	       ((unsigned long long)sb->events_hi << 32)
	       + sb->events_lo);
	printf("\n");
	if (sb->level == 5) {
		c = map_num(r5layout, sb->layout);
		printf("         Layout : %s\n", c?c:"-unknown-");
	}
	if (sb->level == 6) {
		c = map_num(r6layout, sb->layout);
		printf("         Layout : %s\n", c?c:"-unknown-");
	}
	if (sb->level == 10) {
		printf("         Layout :");
		print_r10_layout(sb->layout);
		printf("\n");
	}
	switch(sb->level) {
	case 0:
	case 4:
	case 5:
	case 6:
	case 10:
		printf("     Chunk Size : %dK\n", sb->chunk_size/1024);
		break;
	case -1:
		printf("       Rounding : %dK\n", sb->chunk_size/1024);
		break;
	default: break;
	}
	printf("\n");
	printf("      Number   Major   Minor   RaidDevice State\n");
	for (d= -1; d<(signed int)(sb->raid_disks+delta_extra + sb->spare_disks); d++) {
		mdp_disk_t *dp;
		char *dv;
		char nb[5];
		int wonly;
		if (d>=0) dp = &sb->disks[d];
		else dp = &sb->this_disk;
		snprintf(nb, sizeof(nb), "%4d", d);
		printf("%4s %5d   %5d    %5d    %5d     ", d < 0 ? "this" :  nb,
		       dp->number, dp->major, dp->minor, dp->raid_disk);
		wonly = dp->state & (1<<MD_DISK_WRITEMOSTLY);
		dp->state &= ~(1<<MD_DISK_WRITEMOSTLY);
		if (dp->state & (1<<MD_DISK_FAULTY)) printf(" faulty");
		if (dp->state & (1<<MD_DISK_ACTIVE)) printf(" active");
		if (dp->state & (1<<MD_DISK_SYNC)) printf(" sync");
		if (dp->state & (1<<MD_DISK_REMOVED)) printf(" removed");
		if (wonly) printf(" write-mostly");
		if (dp->state == 0) printf(" spare");
		if ((dv=map_dev(dp->major, dp->minor, 0)))
			printf("   %s", dv);
		printf("\n");
		if (d == -1) printf("\n");
	}
}

static void brief_examine_super0(struct supertype *st, int verbose)
{
	mdp_super_t *sb = st->sb;
	char *c=map_num(pers, sb->level);
	char devname[20];

	sprintf(devname, "/dev/md%d", sb->md_minor);

	if (verbose) {
		printf("ARRAY %s level=%s num-devices=%d",
		       devname,
		       c?c:"-unknown-", sb->raid_disks);
	} else
		printf("ARRAY %s", devname);

	if (sb->minor_version >= 90)
		printf(" UUID=%08x:%08x:%08x:%08x", sb->set_uuid0, sb->set_uuid1,
		       sb->set_uuid2, sb->set_uuid3);
	else
		printf(" UUID=%08x", sb->set_uuid0);
	printf("\n");
}

static void export_examine_super0(struct supertype *st)
{
	mdp_super_t *sb = st->sb;

	printf("MD_LEVEL=%s\n", map_num(pers, sb->level));
	printf("MD_DEVICES=%d\n", sb->raid_disks);
	if (sb->minor_version >= 90)
		printf("MD_UUID=%08x:%08x:%08x:%08x\n",
		       sb->set_uuid0, sb->set_uuid1,
		       sb->set_uuid2, sb->set_uuid3);
	else
		printf("MD_UUID=%08x\n", sb->set_uuid0);
	printf("MD_UPDATE_TIME=%llu\n",
	       __le64_to_cpu(sb->ctime) & 0xFFFFFFFFFFULL);
	printf("MD_EVENTS=%llu\n",
	       ((unsigned long long)sb->events_hi << 32)
	       + sb->events_lo);
}

static void detail_super0(struct supertype *st, char *homehost)
{
	mdp_super_t *sb = st->sb;
	printf("           UUID : ");
	if (sb->minor_version >= 90)
		printf("%08x:%08x:%08x:%08x", sb->set_uuid0, sb->set_uuid1,
		       sb->set_uuid2, sb->set_uuid3);
	else
		printf("%08x", sb->set_uuid0);
	if (homehost) {
		char buf[20];
		void *hash = sha1_buffer(homehost,
					 strlen(homehost),
					 buf);
		if (memcmp(&sb->set_uuid2, hash, 8)==0)
			printf(" (local to host %s)", homehost);
	}
	printf("\n         Events : %d.%d\n\n", sb->events_hi, sb->events_lo);
}

static void brief_detail_super0(struct supertype *st)
{
	mdp_super_t *sb = st->sb;
	printf(" UUID=");
	if (sb->minor_version >= 90)
		printf("%08x:%08x:%08x:%08x", sb->set_uuid0, sb->set_uuid1,
		       sb->set_uuid2, sb->set_uuid3);
	else
		printf("%08x", sb->set_uuid0);
}
#endif

static int match_home0(struct supertype *st, char *homehost)
{
	mdp_super_t *sb = st->sb;
	char buf[20];
	char *hash;

	if (!homehost)
		return 0;
	hash = sha1_buffer(homehost,
			   strlen(homehost),
			   buf);

	return (memcmp(&sb->set_uuid2, hash, 8)==0);
}

static void uuid_from_super0(struct supertype *st, int uuid[4])
{
	mdp_super_t *super = st->sb;
	uuid[0] = super->set_uuid0;
	if (super->minor_version >= 90) {
		uuid[1] = super->set_uuid1;
		uuid[2] = super->set_uuid2;
		uuid[3] = super->set_uuid3;
	} else {
		uuid[1] = 0;
		uuid[2] = 0;
		uuid[3] = 0;
	}
}

static void getinfo_super0(struct supertype *st, struct mdinfo *info)
{
	mdp_super_t *sb = st->sb;
	int working = 0;
	int i;

	info->array.major_version = sb->major_version;
	info->array.minor_version = sb->minor_version;
	info->array.patch_version = sb->patch_version;
	info->array.raid_disks = sb->raid_disks;
	info->array.level = sb->level;
	info->array.layout = sb->layout;
	info->array.md_minor = sb->md_minor;
	info->array.ctime = sb->ctime;
	info->array.utime = sb->utime;
	info->array.chunk_size = sb->chunk_size;
	info->array.state = sb->state;
	info->component_size = sb->size*2;

	info->disk.state = sb->this_disk.state;
	info->disk.major = sb->this_disk.major;
	info->disk.minor = sb->this_disk.minor;
	info->disk.raid_disk = sb->this_disk.raid_disk;
	info->disk.number = sb->this_disk.number;

	info->events = md_event(sb);
	info->data_offset = 0;

	sprintf(info->text_version, "0.%d", sb->minor_version);
	info->safe_mode_delay = 200;

	uuid_from_super0(st, info->uuid);

	if (sb->minor_version > 90 && (sb->reshape_position+1) != 0) {
		info->reshape_active = 1;
		info->reshape_progress = sb->reshape_position;
		info->new_level = sb->new_level;
		info->delta_disks = sb->delta_disks;
		info->new_layout = sb->new_layout;
		info->new_chunk = sb->new_chunk;
		if (info->delta_disks < 0)
			info->array.raid_disks -= info->delta_disks;
	} else
		info->reshape_active = 0;

	sprintf(info->name, "%d", sb->md_minor);
	/* work_disks is calculated rather than read directly */
	for (i=0; i < MD_SB_DISKS; i++)
		if ((sb->disks[i].state & (1<<MD_DISK_SYNC)) &&
		    (sb->disks[i].raid_disk < info->array.raid_disks) &&
		    (sb->disks[i].state & (1<<MD_DISK_ACTIVE)) &&
		    !(sb->disks[i].state & (1<<MD_DISK_FAULTY)))
			working ++;
	info->array.working_disks = working;
}


static int update_super0(struct supertype *st, struct mdinfo *info,
			 char *update,
			 char *devname, int verbose,
			 int uuid_set, char *homehost)
{
	/* NOTE: for 'assemble' and 'force' we need to return non-zero if any change was made.
	 * For others, the return value is ignored.
	 */
	int rv = 0;
	mdp_super_t *sb = st->sb;
	if (strcmp(update, "sparc2.2")==0 ) {
		/* 2.2 sparc put the events in the wrong place
		 * So we copy the tail of the superblock
		 * up 4 bytes before continuing
		 */
		__u32 *sb32 = (__u32*)sb;
		memcpy(sb32+MD_SB_GENERIC_CONSTANT_WORDS+7,
		       sb32+MD_SB_GENERIC_CONSTANT_WORDS+7+1,
		       (MD_SB_WORDS - (MD_SB_GENERIC_CONSTANT_WORDS+7+1))*4);
		if (verbose >= 0)
			fprintf (stderr, Name ": adjusting superblock of %s for 2.2/sparc compatability.\n",
				 devname);
	}
	if (strcmp(update, "super-minor") ==0) {
		sb->md_minor = info->array.md_minor;
		if (verbose > 0)
			fprintf(stderr, Name ": updating superblock of %s with minor number %d\n",
				devname, info->array.md_minor);
	}
	if (strcmp(update, "summaries") == 0) {
		int i;
		/* set nr_disks, active_disks, working_disks,
		 * failed_disks, spare_disks based on disks[]
		 * array in superblock.
		 * Also make sure extra slots aren't 'failed'
		 */
		sb->nr_disks = sb->active_disks =
			sb->working_disks = sb->failed_disks =
			sb->spare_disks = 0;
		for (i=0; i < MD_SB_DISKS ; i++)
			if (sb->disks[i].major ||
			    sb->disks[i].minor) {
				int state = sb->disks[i].state;
				if (state & (1<<MD_DISK_REMOVED))
					continue;
				sb->nr_disks++;
				if (state & (1<<MD_DISK_ACTIVE))
					sb->active_disks++;
				if (state & (1<<MD_DISK_FAULTY))
					sb->failed_disks++;
				else
					sb->working_disks++;
				if (state == 0)
					sb->spare_disks++;
			} else if (i >= sb->raid_disks && sb->disks[i].number == 0)
				sb->disks[i].state = 0;
	}
	if (strcmp(update, "force-one")==0) {
		/* Not enough devices for a working array, so
		 * bring this one up-to-date.
		 */
		__u32 ehi = sb->events_hi, elo = sb->events_lo;
		sb->events_hi = (info->events>>32) & 0xFFFFFFFF;
		sb->events_lo = (info->events) & 0xFFFFFFFF;
		if (sb->events_hi != ehi ||
		    sb->events_lo != elo)
			rv = 1;
	}
	if (strcmp(update, "force-array")==0) {
		/* degraded array and 'force' requested, so
		 * maybe need to mark it 'clean'
		 */
		if ((sb->level == 5 || sb->level == 4 || sb->level == 6) &&
		    (sb->state & (1 << MD_SB_CLEAN)) == 0) {
			/* need to force clean */
			sb->state |= (1 << MD_SB_CLEAN);
			rv = 1;
		}
	}
	if (strcmp(update, "assemble")==0) {
		int d = info->disk.number;
		int wonly = sb->disks[d].state & (1<<MD_DISK_WRITEMOSTLY);
		int mask = (1<<MD_DISK_WRITEMOSTLY);
		int add = 0;
		if (sb->minor_version >= 91)
			/* During reshape we don't insist on everything
			 * being marked 'sync'
			 */
			add = (1<<MD_DISK_SYNC);
		if (((sb->disks[d].state & ~mask) | add)
		    != info->disk.state) {
			sb->disks[d].state = info->disk.state | wonly;
			rv = 1;
		}
	}
	if (strcmp(update, "linear-grow-new") == 0) {
		memset(&sb->disks[info->disk.number], 0, sizeof(sb->disks[0]));
		sb->disks[info->disk.number].number = info->disk.number;
		sb->disks[info->disk.number].major = info->disk.major;
		sb->disks[info->disk.number].minor = info->disk.minor;
		sb->disks[info->disk.number].raid_disk = info->disk.raid_disk;
		sb->disks[info->disk.number].state = info->disk.state;
		sb->this_disk = sb->disks[info->disk.number];
	}
	if (strcmp(update, "linear-grow-update") == 0) {
		sb->raid_disks = info->array.raid_disks;
		sb->nr_disks = info->array.nr_disks;
		sb->active_disks = info->array.active_disks;
		sb->working_disks = info->array.working_disks;
		memset(&sb->disks[info->disk.number], 0, sizeof(sb->disks[0]));
		sb->disks[info->disk.number].number = info->disk.number;
		sb->disks[info->disk.number].major = info->disk.major;
		sb->disks[info->disk.number].minor = info->disk.minor;
		sb->disks[info->disk.number].raid_disk = info->disk.raid_disk;
		sb->disks[info->disk.number].state = info->disk.state;
	}
	if (strcmp(update, "resync") == 0) {
		/* make sure resync happens */
		sb->state &= ~(1<<MD_SB_CLEAN);
		sb->recovery_cp = 0;
	}
	if (strcmp(update, "homehost") == 0 &&
	    homehost) {
		uuid_set = 0;
		update = "uuid";
		info->uuid[0] = sb->set_uuid0;
		info->uuid[1] = sb->set_uuid1;
	}
	if (strcmp(update, "uuid") == 0) {
		if (!uuid_set && homehost) {
			char buf[20];
			char *hash = sha1_buffer(homehost,
						 strlen(homehost),
						 buf);
			memcpy(info->uuid+2, hash, 8);
		}
		sb->set_uuid0 = info->uuid[0];
		sb->set_uuid1 = info->uuid[1];
		sb->set_uuid2 = info->uuid[2];
		sb->set_uuid3 = info->uuid[3];
		if (sb->state & (1<<MD_SB_BITMAP_PRESENT)) {
			struct bitmap_super_s *bm;
			bm = (struct bitmap_super_s*)(sb+1);
			uuid_from_super0(st, (int*)bm->uuid);
		}
	}
	if (strcmp(update, "_reshape_progress")==0)
		sb->reshape_position = info->reshape_progress;

	sb->sb_csum = calc_sb0_csum(sb);
	return rv;
}

/*
 * For verion-0 superblock, the homehost is 'stored' in the
 * uuid.  8 bytes for a hash of the host leaving 8 bytes
 * of random material.
 * We use the first 8 bytes (64bits) of the sha1 of the
 * host name
 */


static int init_super0(struct supertype *st, mdu_array_info_t *info,
		       unsigned long long size, char *ignored_name, char *homehost,
		       int *uuid)
{
	mdp_super_t *sb;
	int spares;

	if (posix_memalign((void**)&sb, 4096,
			   MD_SB_BYTES + ROUND_UP(sizeof(bitmap_super_t), 4096)) != 0) {
		fprintf(stderr, Name ": %s could not allocate superblock\n", __func__);
		return 0;
	}
	memset(sb, 0, MD_SB_BYTES + sizeof(bitmap_super_t));

	st->sb = sb;
	if (info == NULL) {
		/* zeroing the superblock */
		return 0;
	}

	spares = info->working_disks - info->active_disks;
	if (info->raid_disks + spares  > MD_SB_DISKS) {
		fprintf(stderr, Name ": too many devices requested: %d+%d > %d\n",
			info->raid_disks , spares, MD_SB_DISKS);
		return 0;
	}

	sb->md_magic = MD_SB_MAGIC;
	sb->major_version = 0;
	sb->minor_version = 90;
	sb->patch_version = 0;
	sb->gvalid_words = 0; /* ignored */
	sb->ctime = time(0);
	sb->level = info->level;
	if (size != info->size)
		return 0;
	sb->size = info->size;
	sb->nr_disks = info->nr_disks;
	sb->raid_disks = info->raid_disks;
	sb->md_minor = info->md_minor;
	sb->not_persistent = 0;
	if (uuid) {
		sb->set_uuid0 = uuid[0];
		sb->set_uuid1 = uuid[1];
		sb->set_uuid2 = uuid[2];
		sb->set_uuid3 = uuid[3];
	} else {
		int rfd = open("/dev/urandom", O_RDONLY);
		if (rfd < 0 || read(rfd, &sb->set_uuid0, 4) != 4)
			sb->set_uuid0 = random();
		if (rfd < 0 || read(rfd, &sb->set_uuid1, 12) != 12) {
			sb->set_uuid1 = random();
			sb->set_uuid2 = random();
			sb->set_uuid3 = random();
		}
		if (rfd >= 0)
			close(rfd);
	}
	if (homehost) {
		char buf[20];
		char *hash = sha1_buffer(homehost,
					 strlen(homehost),
					 buf);
		memcpy(&sb->set_uuid2, hash, 8);
	}

	sb->utime = sb->ctime;
	sb->state = info->state;
	sb->active_disks = info->active_disks;
	sb->working_disks = info->working_disks;
	sb->failed_disks = info->failed_disks;
	sb->spare_disks = info->spare_disks;
	sb->events_hi = 0;
	sb->events_lo = 1;

	sb->layout = info->layout;
	sb->chunk_size = info->chunk_size;

	return 1;
}

struct devinfo {
	int fd;
	char *devname;
	mdu_disk_info_t disk;
	struct devinfo *next;
};

#ifndef MDASSEMBLE
/* Add a device to the superblock being created */
static int add_to_super0(struct supertype *st, mdu_disk_info_t *dinfo,
			  int fd, char *devname)
{
	mdp_super_t *sb = st->sb;
	mdp_disk_t *dk = &sb->disks[dinfo->number];
	struct devinfo *di, **dip;

	dk->number = dinfo->number;
	dk->major = dinfo->major;
	dk->minor = dinfo->minor;
	dk->raid_disk = dinfo->raid_disk;
	dk->state = dinfo->state;

	sb->this_disk = sb->disks[dinfo->number];
	sb->sb_csum = calc_sb0_csum(sb);

	dip = (struct devinfo **)&st->info;
	while (*dip)
		dip = &(*dip)->next;
	di = malloc(sizeof(struct devinfo));
	di->fd = fd;
	di->devname = devname;
	di->disk = *dinfo;
	di->next = NULL;
	*dip = di;

	return 0;
}
#endif

static int store_super0(struct supertype *st, int fd)
{
	unsigned long long dsize;
	unsigned long long offset;
	mdp_super_t *super = st->sb;

	if (!get_dev_size(fd, NULL, &dsize))
		return 1;

	if (dsize < MD_RESERVED_SECTORS*512)
		return 2;

	offset = MD_NEW_SIZE_SECTORS(dsize>>9);

	offset *= 512;

	if (lseek64(fd, offset, 0)< 0LL)
		return 3;

	if (write(fd, super, sizeof(*super)) != sizeof(*super))
		return 4;

	if (super->state & (1<<MD_SB_BITMAP_PRESENT)) {
		struct bitmap_super_s * bm = (struct bitmap_super_s*)(super+1);
		if (__le32_to_cpu(bm->magic) == BITMAP_MAGIC)
			if (write(fd, bm, ROUND_UP(sizeof(*bm),4096)) != 
			    ROUND_UP(sizeof(*bm),4096))
			    return 5;
	}

	fsync(fd);
	return 0;
}

#ifndef MDASSEMBLE
static int write_init_super0(struct supertype *st)
{
	mdp_super_t *sb = st->sb;
	int rv = 0;
	struct devinfo *di;

	for (di = st->info ; di && ! rv ; di = di->next) {

		if (di->disk.state == 1)
			continue;
		if (di->fd == -1)
			continue;
		Kill(di->devname, 0, 1, 1);
		Kill(di->devname, 0, 1, 1);

		sb->disks[di->disk.number].state &= ~(1<<MD_DISK_FAULTY);

		sb->this_disk = sb->disks[di->disk.number];
		sb->sb_csum = calc_sb0_csum(sb);
		rv = store_super0(st, di->fd);

		if (rv == 0 && (sb->state & (1<<MD_SB_BITMAP_PRESENT)))
			rv = st->ss->write_bitmap(st, di->fd);

		if (rv)
			fprintf(stderr,
				Name ": failed to write superblock to %s\n",
				di->devname);
		close(di->fd);
		di->fd = -1;
	}
	return rv;
}
#endif

static int compare_super0(struct supertype *st, struct supertype *tst)
{
	/*
	 * return:
	 *  0 same, or first was empty, and second was copied
	 *  1 second had wrong number
	 *  2 wrong uuid
	 *  3 wrong other info
	 */
	mdp_super_t *first = st->sb;
	mdp_super_t *second = tst->sb;
	int uuid1[4], uuid2[4];

	if (second->md_magic != MD_SB_MAGIC)
		return 1;
	if (!first) {
		if (posix_memalign((void**)&first, 4096,
			     MD_SB_BYTES + 
			     ROUND_UP(sizeof(struct bitmap_super_s), 4096)) != 0) {
			fprintf(stderr, Name
				": %s could not allocate superblock\n", __func__);
			return 1;
		}
		memcpy(first, second, MD_SB_BYTES + sizeof(struct bitmap_super_s));
		st->sb = first;
		return 0;
	}

	uuid_from_super0(st, uuid1);
	uuid_from_super0(tst, uuid2);
	if (!same_uuid(uuid1, uuid2, 0))
		return 2;
	if (first->major_version != second->major_version ||
	    first->minor_version != second->minor_version ||
	    first->patch_version != second->patch_version ||
	    first->gvalid_words  != second->gvalid_words  ||
	    first->ctime         != second->ctime         ||
	    first->level         != second->level         ||
	    first->size          != second->size          ||
	    first->raid_disks    != second->raid_disks    )
		return 3;

	return 0;
}


static void free_super0(struct supertype *st);

static int load_super0(struct supertype *st, int fd, char *devname)
{
	/* try to read in the superblock
	 * Return:
	 *  0 on success
	 *  1 on cannot get superblock
	 *  2 on superblock meaningless
	 */
	unsigned long long dsize;
	unsigned long long offset;
	mdp_super_t *super;
	int uuid[4];
	struct bitmap_super_s *bsb;

	free_super0(st);

	if (st->subarray[0])
		return 1;

	if (!get_dev_size(fd, devname, &dsize))
		return 1;

	if (dsize < MD_RESERVED_SECTORS*512) {
		if (devname)
			fprintf(stderr, Name
			    ": %s is too small for md: size is %llu sectors.\n",
				devname, dsize);
		return 1;
	}

	offset = MD_NEW_SIZE_SECTORS(dsize>>9);

	offset *= 512;

	ioctl(fd, BLKFLSBUF, 0); /* make sure we read current data */

	if (lseek64(fd, offset, 0)< 0LL) {
		if (devname)
			fprintf(stderr, Name ": Cannot seek to superblock on %s: %s\n",
				devname, strerror(errno));
		return 1;
	}

	if (posix_memalign((void**)&super, 4096,
			   MD_SB_BYTES +
			   ROUND_UP(sizeof(bitmap_super_t), 4096)) != 0) {
		fprintf(stderr, Name
			": %s could not allocate superblock\n", __func__);
		return 1;
	}

	if (read(fd, super, sizeof(*super)) != MD_SB_BYTES) {
		if (devname)
			fprintf(stderr, Name ": Cannot read superblock on %s\n",
				devname);
		free(super);
		return 1;
	}

	if (st->ss && st->minor_version == 9)
		super0_swap_endian(super);

	if (super->md_magic != MD_SB_MAGIC) {
		if (devname)
			fprintf(stderr, Name ": No super block found on %s (Expected magic %08x, got %08x)\n",
				devname, MD_SB_MAGIC, super->md_magic);
		free(super);
		return 2;
	}

	if (super->major_version != 0) {
		if (devname)
			fprintf(stderr, Name ": Cannot interpret superblock on %s - version is %d\n",
				devname, super->major_version);
		free(super);
		return 2;
	}
	st->sb = super;

	if (st->ss == NULL) {
		st->ss = &super0;
		st->minor_version = super->minor_version;
		st->max_devs = MD_SB_DISKS;
		st->info = NULL;
	}

	/* Now check on the bitmap superblock */
	if ((super->state & (1<<MD_SB_BITMAP_PRESENT)) == 0)
		return 0;
	/* Read the bitmap superblock and make sure it looks
	 * valid.  If it doesn't clear the bit.  An --assemble --force
	 * should get that written out.
	 */
	if (read(fd, super+1, ROUND_UP(sizeof(struct bitmap_super_s),4096))
	    != ROUND_UP(sizeof(struct bitmap_super_s),4096))
		goto no_bitmap;

	uuid_from_super0(st, uuid);
	bsb = (struct bitmap_super_s *)(super+1);
	if (__le32_to_cpu(bsb->magic) != BITMAP_MAGIC ||
	    memcmp(bsb->uuid, uuid, 16) != 0)
		goto no_bitmap;
	return 0;

 no_bitmap:
	super->state &= ~(1<<MD_SB_BITMAP_PRESENT);

	return 0;
}

static struct supertype *match_metadata_desc0(char *arg)
{
	struct supertype *st = malloc(sizeof(*st));
	if (!st) return st;

	memset(st, 0, sizeof(*st));
	st->ss = &super0;
	st->info = NULL;
	st->minor_version = 90;
	st->max_devs = MD_SB_DISKS;
	st->sb = NULL;
	/* we sometimes get 00.90 */
	while (arg[0] == '0' && arg[1] == '0')
		arg++;
	if (strcmp(arg, "0") == 0 ||
	    strcmp(arg, "0.90") == 0 ||
	    strcmp(arg, "default") == 0 ||
	    strcmp(arg, "") == 0 /* no metadata */
		)
		return st;

	st->minor_version = 91; /* reshape in progress */
	if (strcmp(arg, "0.91") == 0) /* For dup_super support */
		return st;

	st->minor_version = 9; /* flag for 'byte-swapped' */
	if (strcmp(arg, "0.swap")==0 ||
	    strcmp(arg, "0.9") == 0) /* For dup_super support */
		return st;

	free(st);
	return NULL;
}

static __u64 avail_size0(struct supertype *st, __u64 devsize)
{
	if (devsize < MD_RESERVED_SECTORS)
		return 0ULL;
	return MD_NEW_SIZE_SECTORS(devsize);
}

static int add_internal_bitmap0(struct supertype *st, int *chunkp,
				int delay, int write_behind,
				unsigned long long size, int may_change,
				int major)
{
	/*
	 * The bitmap comes immediately after the superblock and must be 60K in size
	 * at most.  The default size is between 30K and 60K
	 *
	 * size is in sectors,  chunk is in bytes !!!
	 */
	unsigned long long bits;
	unsigned long long max_bits = 60*1024*8;
	unsigned long long min_chunk;
	int chunk = *chunkp;
	mdp_super_t *sb = st->sb;
	bitmap_super_t *bms = (bitmap_super_t*)(((char*)sb) + MD_SB_BYTES);


	min_chunk = 4096; /* sub-page chunks don't work yet.. */
	bits = (size * 512) / min_chunk + 1;
	while (bits > max_bits) {
		min_chunk *= 2;
		bits = (bits+1)/2;
	}
	if (chunk == UnSet)
		chunk = min_chunk;
	else if (chunk < min_chunk)
		return 0; /* chunk size too small */

	sb->state |= (1<<MD_SB_BITMAP_PRESENT);

	memset(bms, 0, sizeof(*bms));
	bms->magic = __cpu_to_le32(BITMAP_MAGIC);
	bms->version = __cpu_to_le32(major);
	uuid_from_super0(st, (int*)bms->uuid);
	bms->chunksize = __cpu_to_le32(chunk);
	bms->daemon_sleep = __cpu_to_le32(delay);
	bms->sync_size = __cpu_to_le64(size);
	bms->write_behind = __cpu_to_le32(write_behind);
	*chunkp = chunk;
	return 1;
}


static void locate_bitmap0(struct supertype *st, int fd)
{
	unsigned long long dsize;
	unsigned long long offset;

	if (!get_dev_size(fd, NULL, &dsize))
		return;

	if (dsize < MD_RESERVED_SECTORS*512)
		return;

	offset = MD_NEW_SIZE_SECTORS(dsize>>9);

	offset *= 512;

	offset += MD_SB_BYTES;

	lseek64(fd, offset, 0);
}

static int write_bitmap0(struct supertype *st, int fd)
{
	unsigned long long dsize;
	unsigned long long offset;
	mdp_super_t *sb = st->sb;

	int rv = 0;

	int towrite, n;
	char abuf[4096+4096];
	char *buf = (char*)(((long)(abuf+4096))&~4095L);

	if (!get_dev_size(fd, NULL, &dsize))
		return 1;


	if (dsize < MD_RESERVED_SECTORS*512)
		return -1;

	offset = MD_NEW_SIZE_SECTORS(dsize>>9);

	offset *= 512;

	if (lseek64(fd, offset + 4096, 0)< 0LL)
		return 3;

	memset(buf, 0xff, 4096);
	memcpy(buf,  ((char*)sb)+MD_SB_BYTES, sizeof(bitmap_super_t));
	towrite = 64*1024;
	while (towrite > 0) {
		n = towrite;
		if (n > 4096)
			n = 4096;
		n = write(fd, buf, n);
		if (n > 0)
			towrite -= n;
		else
			break;
		memset(buf, 0xff, 4096);
	}
	fsync(fd);
	if (towrite)
		rv = -2;

	return rv;
}

static void free_super0(struct supertype *st)
{
	if (st->sb)
		free(st->sb);
	st->sb = NULL;
}

#ifndef MDASSEMBLE
static int validate_geometry0(struct supertype *st, int level,
			      int layout, int raiddisks,
			      int chunk, unsigned long long size,
			      char *subdev, unsigned long long *freesize,
			      int verbose)
{
	unsigned long long ldsize;
	int fd;

	if (level == LEVEL_CONTAINER)
		return 0;
	if (raiddisks > MD_SB_DISKS)
		return 0;
	if (size > (0x7fffffffULL<<9))
		return 0;
	if (!subdev)
		return 1;

	fd = open(subdev, O_RDONLY|O_EXCL, 0);
	if (fd < 0) {
		if (verbose)
			fprintf(stderr, Name ": super0.90 cannot open %s: %s\n",
				subdev, strerror(errno));
		return 0;
	}

	if (!get_dev_size(fd, subdev, &ldsize)) {
		close(fd);
		return 0;
	}
	close(fd);

	if (ldsize < MD_RESERVED_SECTORS * 512)
		return 0;
	if (size > (0x7fffffffULL<<9))
		return 0;
	*freesize = MD_NEW_SIZE_SECTORS(ldsize >> 9);
	return 1;
}
#endif /* MDASSEMBLE */

struct superswitch super0 = {
#ifndef MDASSEMBLE
	.examine_super = examine_super0,
	.brief_examine_super = brief_examine_super0,
	.export_examine_super = export_examine_super0,
	.detail_super = detail_super0,
	.brief_detail_super = brief_detail_super0,
	.write_init_super = write_init_super0,
	.validate_geometry = validate_geometry0,
	.add_to_super = add_to_super0,
#endif
	.match_home = match_home0,
	.uuid_from_super = uuid_from_super0,
	.getinfo_super = getinfo_super0,
	.update_super = update_super0,
	.init_super = init_super0,
	.store_super = store_super0,
	.compare_super = compare_super0,
	.load_super = load_super0,
	.match_metadata_desc = match_metadata_desc0,
	.avail_size = avail_size0,
	.add_internal_bitmap = add_internal_bitmap0,
	.locate_bitmap = locate_bitmap0,
	.write_bitmap = write_bitmap0,
	.free_super = free_super0,
	.name = "0.90",
};
