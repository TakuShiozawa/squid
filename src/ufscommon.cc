/*
 * $Id: ufscommon.cc,v 1.6 2003/01/09 11:49:35 hno Exp $
 *
 * DEBUG: section 47    Store Directory Routines
 * AUTHOR: Duane Wessels
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "ufscommon.h"
#include "Store.h"
#include "RefCount.h"

CBDATA_CLASS_INIT(RebuildState);

void *
RebuildState::operator new (size_t size)
{
    assert (size == sizeof(RebuildState));
    CBDATA_INIT_TYPE(RebuildState);
    RebuildState *result = cbdataAlloc(RebuildState);
    /* Mark result as being owned - we want the refcounter to do the delete
     * call */
    cbdataReference(result);
    return result;
}
 
void
RebuildState::operator delete (void *address)
{
    RebuildState *t = static_cast<RebuildState *>(address);
    cbdataFree(address);
    /* And allow the memory to be freed */
    cbdataReferenceDone (t);
}

void
RebuildState::deleteSelf() const
{
    delete this;
}

RebuildState::~RebuildState()
{
    store_dirs_rebuilding--;
    sd->closeTmpSwapLog();
    storeRebuildComplete(&counts);
}

void
RebuildState::RebuildFromDirectory(void *data)
{
    RebuildState *rb = (RebuildState *)data;
    rb->rebuildFromDirectory();
}

void
RebuildState::rebuildFromDirectory()
{
    LOCAL_ARRAY(char, hdr_buf, SM_PAGE_SIZE);
    StoreEntry *e = NULL;
    StoreEntry tmpe;
    cache_key key[MD5_DIGEST_CHARS];
    struct stat sb;
    int swap_hdr_len;
    int fd = -1;
    tlv *tlv_list;
    tlv *t;
    assert(this != NULL);
    debug(47, 3) ("commonUfsDirRebuildFromDirectory: DIR #%d\n", sd->index);
    for (int count = 0; count < speed; count++) {
	assert(fd == -1);
	sfileno filn = 0;
	int size;
	fd = getNextFile(&filn, &size);
	if (fd == -2) {
	    debug(47, 1) ("Done scanning %s swaplog (%d entries)\n",
		sd->path, n_read);
	    deleteSelf();
	    return;
	} else if (fd < 0) {
	    continue;
	}
	assert(fd > -1);
	/* lets get file stats here */
	if (fstat(fd, &sb) < 0) {
	    debug(47, 1) ("commonUfsDirRebuildFromDirectory: fstat(FD %d): %s\n",
		fd, xstrerror());
	    file_close(fd);
	    store_open_disk_fd--;
	    fd = -1;
	    continue;
	}
	if ((++counts.scancount & 0xFFFF) == 0)
	    debug(47, 3) ("  %s %7d files opened so far.\n",
		sd->path, counts.scancount);
	debug(47, 9) ("file_in: fd=%d %08X\n", fd, filn);
	statCounter.syscalls.disk.reads++;
	if (FD_READ_METHOD(fd, hdr_buf, SM_PAGE_SIZE) < 0) {
	    debug(47, 1) ("commonUfsDirRebuildFromDirectory: read(FD %d): %s\n",
		fd, xstrerror());
	    file_close(fd);
	    store_open_disk_fd--;
	    fd = -1;
	    continue;
	}
	file_close(fd);
	store_open_disk_fd--;
	fd = -1;
	swap_hdr_len = 0;
#if USE_TRUNCATE
	if (sb.st_size == 0)
	    continue;
#endif
	tlv_list = storeSwapMetaUnpack(hdr_buf, &swap_hdr_len);
	if (tlv_list == NULL) {
	    debug(47, 1) ("commonUfsDirRebuildFromDirectory: failed to get meta data\n");
	    /* XXX shouldn't this be a call to commonUfsUnlink ? */
	    sd->unlinkFile (filn);
	    continue;
	}
	debug(47, 3) ("commonUfsDirRebuildFromDirectory: successful swap meta unpacking\n");
	memset(key, '\0', MD5_DIGEST_CHARS);
	memset(&tmpe, '\0', sizeof(StoreEntry));
	for (t = tlv_list; t; t = t->next) {
	    switch (t->type) {
	    case STORE_META_KEY:
		assert(t->length == MD5_DIGEST_CHARS);
		xmemcpy(key, t->value, MD5_DIGEST_CHARS);
		break;
	    case STORE_META_STD:
		assert(t->length == STORE_HDR_METASIZE);
		xmemcpy(&tmpe.timestamp, t->value, STORE_HDR_METASIZE);
		break;
	    default:
		break;
	    }
	}
	storeSwapTLVFree(tlv_list);
	tlv_list = NULL;
	if (storeKeyNull(key)) {
	    debug(47, 1) ("commonUfsDirRebuildFromDirectory: NULL key\n");
	    sd->unlinkFile(filn);
	    continue;
	}
	tmpe.key = key;
	/* check sizes */
	if (tmpe.swap_file_sz == 0) {
	    tmpe.swap_file_sz = (size_t) sb.st_size;
	} else if (tmpe.swap_file_sz == (size_t)(sb.st_size - swap_hdr_len)) {
	    tmpe.swap_file_sz = (size_t) sb.st_size;
	} else if (tmpe.swap_file_sz != (size_t)sb.st_size) {
	    debug(47, 1) ("commonUfsDirRebuildFromDirectory: SIZE MISMATCH %ld!=%ld\n",
		(long int) tmpe.swap_file_sz, (long int) sb.st_size);
	    sd->unlinkFile(filn);
	    continue;
	}
	if (EBIT_TEST(tmpe.flags, KEY_PRIVATE)) {
	    sd->unlinkFile(filn);
	    counts.badflags++;
	    continue;
	}
	e = storeGet(key);
	if (e && e->lastref >= tmpe.lastref) {
	    /* key already exists, current entry is newer */
	    /* keep old, ignore new */
	    counts.dupcount++;
	    continue;
	} else if (NULL != e) {
	    /* URL already exists, this swapfile not being used */
	    /* junk old, load new */
	    storeRelease(e);	/* release old entry */
	    counts.dupcount++;
	}
	counts.objcount++;
	storeEntryDump(&tmpe, 5);
	e = sd->addDiskRestore(key,
	    filn,
	    tmpe.swap_file_sz,
	    tmpe.expires,
	    tmpe.timestamp,
	    tmpe.lastref,
	    tmpe.lastmod,
	    tmpe.refcount,	/* refcount */
	    tmpe.flags,		/* flags */
	    (int) flags.clean);
	storeDirSwapLog(e, SWAP_LOG_ADD);
    }
    eventAdd("storeRebuild", RebuildFromDirectory, this, 0.0, 1);
}

void
RebuildState::RebuildFromSwapLog(void *data)
{
    RebuildState *rb = (RebuildState *)data;
    rb->rebuildFromSwapLog();
}

void
RebuildState::rebuildFromSwapLog()
{
    StoreEntry *e = NULL;
    double x;
    /* load a number of objects per invocation */
    for (int count = 0; count < speed; count++) {
        storeSwapLogData s;
        size_t ss = sizeof(storeSwapLogData);
	if (fread(&s, ss, 1, log) != 1) {
	    debug(47, 1) ("Done reading %s swaplog (%d entries)\n",
		sd->path, n_read);
	    fclose(log);
	    log = NULL;
	    delete this;
	    return;
	}
	n_read++;
	if (s.op <= SWAP_LOG_NOP)
	    continue;
	if (s.op >= SWAP_LOG_MAX)
	    continue;
	/*
	 * BC: during 2.4 development, we changed the way swap file
	 * numbers are assigned and stored.  The high 16 bits used
	 * to encode the SD index number.  There used to be a call
	 * to storeDirProperFileno here that re-assigned the index 
	 * bits.  Now, for backwards compatibility, we just need
	 * to mask it off.
	 */
	s.swap_filen &= 0x00FFFFFF;
	debug(47, 3) ("commonUfsDirRebuildFromSwapLog: %s %s %08X\n",
	    swap_log_op_str[(int) s.op],
	    storeKeyText(s.key),
	    s.swap_filen);
	if (s.op == SWAP_LOG_ADD) {
	    (void) 0;
	} else if (s.op == SWAP_LOG_DEL) {
	    /* Delete unless we already have a newer copy */
	    if ((e = storeGet(s.key)) != NULL && s.lastref > e->lastref) {
		/*
		 * Make sure we don't unlink the file, it might be
		 * in use by a subsequent entry.  Also note that
		 * we don't have to subtract from store_swap_size
		 * because adding to store_swap_size happens in
		 * the cleanup procedure.
		 */
		storeExpireNow(e);
		storeReleaseRequest(e);
		if (e->swap_filen > -1) {
		    sd->replacementRemove(e);
		    sd->mapBitReset(e->swap_filen);
		    e->swap_filen = -1;
		    e->swap_dirn = -1;
		}
		storeRelease(e);
		counts.objcount--;
		counts.cancelcount++;
	    }
	    continue;
	} else {
	    x = ::log(++counts.bad_log_op) / ::log(10.0);
	    if (0.0 == x - (double) (int) x)
		debug(47, 1) ("WARNING: %d invalid swap log entries found\n",
		    counts.bad_log_op);
	    counts.invalid++;
	    continue;
	}
	if ((++counts.scancount & 0xFFF) == 0) {
	    struct stat sb;
	    if (0 == fstat(fileno(log), &sb))
		storeRebuildProgress(sd->index,
		    (int) sb.st_size / ss, n_read);
	}
	if (!sd->validFileno(s.swap_filen, 0)) {
	    counts.invalid++;
	    continue;
	}
	if (EBIT_TEST(s.flags, KEY_PRIVATE)) {
	    counts.badflags++;
	    continue;
	}
	e = storeGet(s.key);
        int used;			/* is swapfile already in use? */
	used = sd->mapBitTest(s.swap_filen);
	/* If this URL already exists in the cache, does the swap log
	 * appear to have a newer entry?  Compare 'lastref' from the
	 * swap log to e->lastref. */
    	/* is the log entry newer than current entry? */
	int disk_entry_newer = e ? (s.lastref > e->lastref ? 1 : 0) : 0;
	if (used && !disk_entry_newer) {
	    /* log entry is old, ignore it */
	    counts.clashcount++;
	    continue;
	} else if (used && e && e->swap_filen == s.swap_filen && e->swap_dirn == sd->index) {
	    /* swapfile taken, same URL, newer, update meta */
	    if (e->store_status == STORE_OK) {
		e->lastref = s.timestamp;
		e->timestamp = s.timestamp;
		e->expires = s.expires;
		e->lastmod = s.lastmod;
		e->flags = s.flags;
		e->refcount += s.refcount;
		sd->dereference(*e);
	    } else {
		debug_trap("commonUfsDirRebuildFromSwapLog: bad condition");
		debug(47, 1) ("\tSee %s:%d\n", __FILE__, __LINE__);
	    }
	    continue;
	} else if (used) {
	    /* swapfile in use, not by this URL, log entry is newer */
	    /* This is sorta bad: the log entry should NOT be newer at this
	     * point.  If the log is dirty, the filesize check should have
	     * caught this.  If the log is clean, there should never be a
	     * newer entry. */
	    debug(47, 1) ("WARNING: newer swaplog entry for dirno %d, fileno %08X\n",
		sd->index, s.swap_filen);
	    /* I'm tempted to remove the swapfile here just to be safe,
	     * but there is a bad race condition in the NOVM version if
	     * the swapfile has recently been opened for writing, but
	     * not yet opened for reading.  Because we can't map
	     * swapfiles back to StoreEntrys, we don't know the state
	     * of the entry using that file.  */
	    /* We'll assume the existing entry is valid, probably because
	     * were in a slow rebuild and the the swap file number got taken
	     * and the validation procedure hasn't run. */
	    assert(flags.need_to_validate);
	    counts.clashcount++;
	    continue;
	} else if (e && !disk_entry_newer) {
	    /* key already exists, current entry is newer */
	    /* keep old, ignore new */
	    counts.dupcount++;
	    continue;
	} else if (e) {
	    /* key already exists, this swapfile not being used */
	    /* junk old, load new */
	    storeExpireNow(e);
	    storeReleaseRequest(e);
	    if (e->swap_filen > -1) {
		sd->replacementRemove(e);
		/* Make sure we don't actually unlink the file */
		sd->mapBitReset(e->swap_filen);
		e->swap_filen = -1;
		e->swap_dirn = -1;
	    }
	    storeRelease(e);
	    counts.dupcount++;
	} else {
	    /* URL doesnt exist, swapfile not in use */
	    /* load new */
	    (void) 0;
	}
	/* update store_swap_size */
	counts.objcount++;
	e = sd->addDiskRestore(s.key,
	    s.swap_filen,
	    s.swap_file_sz,
	    s.expires,
	    s.timestamp,
	    s.lastref,
	    s.lastmod,
	    s.refcount,
	    s.flags,
	    (int) flags.clean);
	storeDirSwapLog(e, SWAP_LOG_ADD);
    }
    eventAdd("storeRebuild", RebuildFromSwapLog, this, 0.0, 1);
}

int
RebuildState::getNextFile(sfileno * filn_p, int *size)
{
    int fd = -1;
    int dirs_opened = 0;
    debug(47, 3) ("commonUfsDirGetNextFile: flag=%d, %d: /%02X/%02X\n",
	flags.init,
	sd->index,
	curlvl1,
	curlvl2);
    if (done)
	return -2;
    while (fd < 0 && done == 0) {
	fd = -1;
	if (0 == flags.init) {	/* initialize, open first file */
	    done = 0;
	    curlvl1 = 0;
	    curlvl2 = 0;
	    in_dir = 0;
	    flags.init = 1;
	    assert(Config.cacheSwap.n_configured > 0);
	}
	if (0 == in_dir) {	/* we need to read in a new directory */
	    snprintf(fullpath, SQUID_MAXPATHLEN, "%s/%02X/%02X",
		sd->path,
		curlvl1, curlvl2);
	    if (dirs_opened)
		return -1;
	    td = opendir(fullpath);
	    dirs_opened++;
	    if (td == NULL) {
		debug(47, 1) ("commonUfsDirGetNextFile: opendir: %s: %s\n",
		    fullpath, xstrerror());
	    } else {
		entry = readdir(td);	/* skip . and .. */
		entry = readdir(td);
		if (entry == NULL && errno == ENOENT)
		    debug(47, 1) ("commonUfsDirGetNextFile: directory does not exist!.\n");
		debug(47, 3) ("commonUfsDirGetNextFile: Directory %s\n", fullpath);
	    }
	}
	if (td != NULL && (entry = readdir(td)) != NULL) {
	    in_dir++;
	    if (sscanf(entry->d_name, "%x", &fn) != 1) {
		debug(47, 3) ("commonUfsDirGetNextFile: invalid %s\n",
		    entry->d_name);
		continue;
	    }
	    if (!UFSSwapDir::FilenoBelongsHere(fn, sd->index, curlvl1, curlvl2)) {
		debug(47, 3) ("commonUfsDirGetNextFile: %08X does not belong in %d/%d/%d\n",
		    fn, sd->index, curlvl1, curlvl2);
		continue;
	    }
	    if (sd->mapBitTest(fn)) {
		debug(47, 3) ("commonUfsDirGetNextFile: Locked, continuing with next.\n");
		continue;
	    }
	    snprintf(fullfilename, SQUID_MAXPATHLEN, "%s/%s",
		fullpath, entry->d_name);
	    debug(47, 3) ("commonUfsDirGetNextFile: Opening %s\n", fullfilename);
	    fd = file_open(fullfilename, O_RDONLY | O_BINARY);
	    if (fd < 0)
		debug(47, 1) ("commonUfsDirGetNextFile: %s: %s\n", fullfilename, xstrerror());
	    else
		store_open_disk_fd++;
	    continue;
	}
	if (td != NULL)
	    closedir(td);
	td = NULL;
	in_dir = 0;
	if (sd->validL2(++curlvl2))
	    continue;
	curlvl2 = 0;
	if (sd->validL1(++curlvl1))
	    continue;
	curlvl1 = 0;
	done = 1;
    }
    *filn_p = fn;
    return fd;
}
