/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/** \file
 * \brief
 * CAN over EtherCAT (CoE) module.
 *
 * SDO read / write and SDO service functions
 */

#include <linux/string.h>
#include "include/sys/gcc/cc.h"
#include "esc.h"
#include "esc_coe.h"

#define BITS2BYTES(b) (((b) + 7U) >> 3)
#define BITSPOS2BYTESOFFSET(b) ((b) >> 3)

/* Fetch value from object dictionary */
#define OBJ_VALUE_FETCH(v, o) \
	((o).data ? *(__typeof__(v) *)(o).data : (__typeof__(v))(o).value)

#define SDO_COMMAND(byte) \
	((byte) & 0xe0)
#define SDO_COMPLETE_ACCESS(byte) \
	(((byte) & COE_COMPLETEACCESS) == COE_COMPLETEACCESS)

#define READ_ACCESS(access, state) \
	((((access) & ATYPE_Rpre) && ((state) == ESCpreop)) || \
	(((access) & ATYPE_Rsafe) && ((state) == ESCsafeop)) || \
	(((access) & ATYPE_Rop) && ((state) == ESCop)))

#define WRITE_ACCESS(access, state) \
	((((access) & ATYPE_Wpre) && ((state) == ESCpreop)) || \
	(((access) & ATYPE_Wsafe) && ((state) == ESCsafeop)) || \
	(((access) & ATYPE_Wop) && ((state) == ESCop)))

enum load_t { UPLOAD, DOWNLOAD };

/** Search for an object sub-index.
 *
 * @param[in] nidx   = local array index of object we want to find sub-index to
 * @param[in] subindex   = value on sub-index of object we want to locate
 * @return local array index if we succeed, -1 if we didn't find the index.
 */
s16 SDO_findsubindex(s32 nidx, u8 subindex)
{
	const struct _objd *objd;
	s16 n = 0;
	u8 maxsub;

	objd = SDOobjects[nidx].objdesc;
	maxsub = SDOobjects[nidx].maxsub;

	/* Since most objects contain all subindexes (i.e. are not sparse),
	 * check the most likely scenario first
	 */
	if (subindex <= maxsub && ((objd + subindex)->subindex == subindex))
		return subindex;

	while (((objd + n)->subindex < subindex) && (n < maxsub))
		n++;

	if ((objd + n)->subindex != subindex)
		return -1;

	return n;
}

/** Search for an object index matching the wanted value in the Object List.
 *
 * @param[in] index   = value on index of object we want to locate
 * @return local array index if we succeed, -1 if we didn't find the index.
 */
s32 SDO_findobject(u16 index)
{
	s32 n = 0;

	while (SDOobjects[n].index < index)
		n++;

	if (SDOobjects[n].index != index)
		return -1;

	return n;
}

/**
 * Calculate the size in Bytes of RxPDO or TxPDOs by adding the
 * objects in SyncManager SDO 1C1x.
 *
 * A list of mapped objects is created for fast lookup of
 * dynamically mapped process data. The max size of the list (@a
 * max_mappings) can be set to 0 if dynamic processdata is not
 * supported.
 *
 * The output variable @a nmappings is set to 0 if dynamic processdata
 * is not supported. It is set to the number of mapped objects if
 * dynamic processdata is supported, or -1 if the mapping was
 * incorrect.

 * @param[in] index = SM index
 * @param[out] nmappings = number of mapped objects in SM, or -1 if
 *   mapping is invalid
 * @param[out] mappings = list of mapped objects in SM
 * @param[out] max_mappings = max number of mapped objects in SM
 * @return size of RxPDO or TxPDOs in Bytes.
 */
u16 sizeOfPDO(u16 index, int *nmappings, struct _SMmap *mappings,
	      int max_mappings)
{
	u32 offset = 0;
	u16 hobj;
	u8 si, sic, c;
	s32 nidx;
	const struct _objd *objd;
	const struct _objd *objd1c1x;
	int mapIx = 0;

	if (index != RX_PDO_OBJIDX && index != TX_PDO_OBJIDX)
		return 0;

	nidx = SDO_findobject(index);
	if (nidx < 0)
		return 0;

	objd1c1x = SDOobjects[nidx].objdesc;

	si = OBJ_VALUE_FETCH(si, objd1c1x[0]);
	if (si) {
		for (sic = 1; sic <= si; sic++) {
			hobj = OBJ_VALUE_FETCH(hobj, objd1c1x[sic]);
			nidx = SDO_findobject(hobj);
			if (nidx >= 0) {
				u8 maxsub;

				objd = SDOobjects[nidx].objdesc;
				maxsub = OBJ_VALUE_FETCH(maxsub, objd[0]);

				for (c = 1; c <= maxsub; c++) {
					u32 value = OBJ_VALUE_FETCH(value, objd[c]);
					u8 bitlength = value & 0xFF;

					if (max_mappings > 0) {
						u16 index = (u16)(value >> 16);
						u8 subindex = (value >> 8) & 0xFF;
						const struct _objd *mapping;

						if (mapIx == max_mappings) {
							/* Too many mapped objects */
							*nmappings = -1;
							return 0;
						}

						DPRINT("%04" PRIx32 ":%02" PRIx32 " @ %" PRIu32
						       "\n",
						       index,
						       subindex,
						       offset);

						if (index == 0 && subindex == 0) {
							/* Padding element */
							mapping = NULL;
						} else {
							nidx = SDO_findobject(index);
							if (nidx >= 0) {
								s16 nsub;

								nsub = SDO_findsubindex(nidx,
											subindex);
								if (nsub < 0) {
									/* Mapped subindex does
									 * not exist
									 */
									*nmappings = -1;
									return 0;
								}

								mapping =
								&SDOobjects[nidx].objdesc[nsub];
							} else {
								/* Mapped index does not exist */
								*nmappings = -1;
								return 0;
							}
						}

						mappings[mapIx].obj = mapping;
						/* Save object list reference */
						if (mapping)
							mappings[mapIx].objectlistitem =
								&SDOobjects[nidx];
						else
							mappings[mapIx].objectlistitem = NULL;

						mappings[mapIx++].offset = offset;
					}

					offset += bitlength;
				}
			}
		}
	}

	if (max_mappings > 0)
		*nmappings = mapIx;
	else
		*nmappings = 0;

	return BITS2BYTES(offset) & 0xFFFF;
}

/** Copy to mailbox.
 *
 * @param[in] source = pointer to source
 * @param[in] dest   = pointer to destination
 * @param[in] size   = Size to copy
 */
static void copy2mbx(void *source, void *dest, size_t size)
{
	memcpy(dest, source, size);
}

/** Function for sending an SDO Abort reply.
 *
 * @param[in] reusembx   = mailbox buffer to use (if 0 then claim a new buffer)
 * @param[in] index      = index of object causing abort reply
 * @param[in] sub-index  = sub-index of object causing abort reply
 * @param[in] abortcode  = abort code to send in reply
 */
static void SDO_abort(u8 reusembx, u16 index, u8 subindex, u32 abortcode)
{
	u8 MBXout;
	struct _COEsdo *coeres;

	if (reusembx)
		MBXout = reusembx;
	else
		MBXout = ESC_claimbuffer();

	if (MBXout) {
		coeres = (struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];
		coeres->mbxheader.length = htoes(COE_DEFAULTLENGTH);
		coeres->mbxheader.mbxtype = MBXCOE;
		coeres->coeheader.numberservice =
			htoes((0 & 0x01f) | (COE_SDOREQUEST << 12));
		coeres->index = htoes(index);
		coeres->subindex = subindex;
		coeres->command = COE_COMMAND_SDOABORT;
		coeres->size = htoel(abortcode);
		MBXcontrol[MBXout].state = MBXstate_outreq;
	}
}

static void set_state_idle(u8 reusembx,
			   u16 index,
			   u8 subindex,
			   u32 abortcode)
{
	if (abortcode != 0)
		SDO_abort(reusembx, index, subindex, abortcode);

	MBXcontrol[0].state = MBXstate_idle;
	ESCvar.xoe = 0;
}

/** Function for responding on requested SDO Upload, sending the content
 *  requested in a free Mailbox buffer. Depending of size of data expedited,
 *  normal or segmented transfer is used. On error an SDO Abort will be sent.
 */
static void SDO_upload(void)
{
	struct _COEsdo *coesdo, *coeres;
	u16 index;
	u8 subindex;
	s32 nidx;
	s16 nsub;
	u8 MBXout;
	u32 size;
	u8 dss;
	u32 abort = 1;
	const struct _objd *objd;
	u8 access;
	u8 state;
	void *dataptr;

	coesdo = (struct _COEsdo *)&MBX[0];
	index = etohs(coesdo->index);
	subindex = coesdo->subindex;
	nidx = SDO_findobject(index);
	if (nidx >= 0) {
		nsub = SDO_findsubindex(nidx, subindex);
		if (nsub >= 0) {
			objd = SDOobjects[nidx].objdesc;
			access = (objd + nsub)->flags & 0x3f;
			state = ESCvar.ALstatus & 0x0f;

			if (!READ_ACCESS(access, state)) {
				set_state_idle(0, index, subindex, ABORT_WRITEONLY);
				return;
			}
			MBXout = ESC_claimbuffer();
			if (MBXout) {
				coeres = (struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];
				coeres->mbxheader.length = htoes(COE_DEFAULTLENGTH);
				coeres->mbxheader.mbxtype = MBXCOE;
				coeres->coeheader.numberservice =
					htoes((0 & 0x01f) | (COE_SDORESPONSE << 12));
				size = (objd + nsub)->bitlength;
				/* expedited bits used calculation */
				dss = 0x0c;
				if (size > 8)
					dss = 0x08;
				if (size > 16)
					dss = 0x04;
				if (size > 24)
					dss = 0x00;

				coeres->index = htoes(index);
				coeres->subindex = subindex;
				coeres->command = COE_COMMAND_UPLOADRESPONSE |
						  COE_SIZE_INDICATOR;
				/* convert bits to bytes */
				size = BITS2BYTES(size);
				if (size <= 4) {
					/* expedited response i.e. length<=4 bytes */
					coeres->command |= (COE_EXPEDITED_INDICATOR | dss);
					dataptr = ((objd + nsub)->data) ?
							(objd + nsub)->data :
							(void *)&((objd + nsub)->value);
					abort = ESC_upload_pre_objecthandler(index,
									     subindex,
									     dataptr,
									     (size_t *)&size,
									     (objd + nsub)->flags);
					if (abort == 0) {
						if (!(objd + nsub)->data)
							/* use constant value */
							coeres->size = htoel((objd + nsub)->value);
						else
							/* use dynamic data */
							copy2mbx((objd + nsub)->data,
								 &coeres->size,
								 size);
					} else {
						set_state_idle(MBXout, index, subindex, abort);
						return;
					}
				} else {
					/* normal response i.e. length>4 bytes */
					abort = ESC_upload_pre_objecthandler(index,
									     subindex,
									     (objd + nsub)->data,
									     (size_t *)&size,
									     (objd + nsub)->flags);
					if (abort == 0) {
						/* set total size in bytes */
						ESCvar.frags = size;
						coeres->size = htoel(size);
						if ((size + COE_HEADERSIZE) > ESC_MBXDSIZE) {
							/* segmented transfer needed */
							/* limit to mailbox size */
							size = ESC_MBXDSIZE - COE_HEADERSIZE;
							/* number of bytes done */
							ESCvar.fragsleft = size;
							/* signal segmented transfer */
							ESCvar.segmented = MBXSEU;
							ESCvar.data = (objd + nsub)->data;
							ESCvar.flags = (objd + nsub)->flags;
						} else {
							ESCvar.segmented = 0;
						}
						coeres->mbxheader.length =
								htoes(COE_HEADERSIZE + size);

						/* use dynamic data */
						copy2mbx((objd + nsub)->data,
							 (&coeres->size) + 1,
							 size);
					} else {
						set_state_idle(MBXout, index, subindex, abort);
						return;
					}
				}
				if (abort == 0 && ESCvar.segmented == 0) {
					abort = ESC_upload_post_objecthandler(index,
									      subindex,
									      (objd + nsub)->flags);
					if (abort != 0) {
						set_state_idle(MBXout, index, subindex, abort);
						return;
					}
				}
				MBXcontrol[MBXout].state = MBXstate_outreq;
			}
		} else {
			SDO_abort(0, index, subindex, ABORT_NOSUBINDEX);
		}
	} else {
		SDO_abort(0, index, subindex, ABORT_NOOBJECT);
	}
	MBXcontrol[0].state = MBXstate_idle;
	ESCvar.xoe = 0;
}

static u32 complete_access_get_variables(struct _COEsdo *coesdo, u16 *index,
					 u8 *subindex, s32 *nidx,
					 s16 *nsub)
{
	*index = etohs(coesdo->index);
	*subindex = coesdo->subindex;

	/* A Complete Access must start with Subindex 0 or Subindex 1 */
	if (*subindex > 1)
		return ABORT_UNSUPPORTED;

	*nidx = SDO_findobject(*index);
	if (*nidx < 0)
		return ABORT_NOOBJECT;

	*nsub = SDO_findsubindex(*nidx, *subindex);
	if (*nsub < 0)
		return ABORT_NOSUBINDEX;

	return 0;
}

static u32 complete_access_subindex_loop(const struct _objd *objd,
					 s32 nidx,
					 s16 nsub,
					 u8 *mbxdata,
					 enum load_t load_type,
					 u32 max_bytes)
{
	u32 size = 0;

	/* Objects with dynamic entries cannot be accessed with Complete Access */
	if (objd->datatype == DTYPE_VISIBLE_STRING ||
	    objd->datatype == DTYPE_OCTET_STRING   ||
	    objd->datatype == DTYPE_UNICODE_STRING)
		return ABORT_CA_NOT_SUPPORTED;

	/* Clear padded mbxdata byte [1] on upload */
	if (load_type == UPLOAD && (mbxdata))
		mbxdata[1] = 0;

	while (nsub <= SDOobjects[nidx].maxsub) {
		u16 bitlen = (objd + nsub)->bitlength;
		void *ul_source = ((objd + nsub)->data) ?
				  (objd + nsub)->data :
				  (void *)&((objd + nsub)->value);
		u8 bitoffset = size % 8;
		u8 access = (objd + nsub)->flags & 0x3f;
		u8 state = ESCvar.ALstatus & 0x0f;

		if ((bitlen % 8) == 0) {
			if (bitoffset != 0)
				/* move on to next byte boundary */
				size += (8U - bitoffset);

			if (mbxdata) {
				/* copy a non-bit data type to a byte boundary */
				if (load_type == UPLOAD) {
					if (READ_ACCESS(access, state))
						memcpy(&mbxdata[BITS2BYTES(size)],
						       ul_source,
						       BITS2BYTES(bitlen));
					else
						/* return zeroes for upload of WO objects */
						memset(&mbxdata[BITS2BYTES(size)],
						       0,
						       BITS2BYTES(bitlen));
				}
				/* download of RO objects shall be ignored */
				else if (WRITE_ACCESS(access, state))
					memcpy((objd + nsub)->data,
					       &mbxdata[BITS2BYTES(size)],
					       BITS2BYTES(bitlen));
			}
		} else if ((load_type == UPLOAD) && (mbxdata)) {
			/* copy a bit data type into correct position */
			u32 bitmask = (1U << bitlen) - 1U;
			u32 tempmask;

			if (READ_ACCESS(access, state)) {
				if (bitoffset == 0)
					mbxdata[BITSPOS2BYTESOFFSET(size)] = 0;
				tempmask = (*(u8 *)ul_source & bitmask) << bitoffset;
				mbxdata[BITSPOS2BYTESOFFSET(size)] |= (u8)tempmask;
			} else {
				tempmask = ~(bitmask << bitoffset);
				mbxdata[BITSPOS2BYTESOFFSET(size)] &= (u8)tempmask;
			}
		}

		/* Subindex 0 is padded to 16 bit if not object type VARIABLE.
		 * For VARIABLE use true bitsize.
		 */
		size +=
		((nsub == 0) && (SDOobjects[nidx].objtype != OTYPE_VAR)) ? 16 : bitlen;
		nsub++;

		if (max_bytes > 0 && (BITS2BYTES(size) >= max_bytes))
			break;
	}

	return size;
}

static void init_coesdo(struct _COEsdo *coesdo,
			u8 sdoservice,
			u8 command,
			u16 index,
			u8 subindex)
{
	coesdo->mbxheader.length = htoes(COE_DEFAULTLENGTH);
	coesdo->mbxheader.mbxtype = MBXCOE;
	coesdo->coeheader.numberservice = htoes(sdoservice << 12);
	coesdo->command = command;
	coesdo->index = htoes(index);
	coesdo->subindex = subindex;
}

/** Function for responding on requested SDO Upload with Complete Access,
 *  sending the content requested in a free Mailbox buffer. Depending of
 *  size of data expedited, normal or segmented transfer is used.
 *  On error an SDO Abort will be sent.
 */
static void SDO_upload_complete_access(void)
{
	struct _COEsdo *coesdo = (struct _COEsdo *)&MBX[0];
	u16 index;
	u8 subindex;
	s32 nidx;
	s16 nsub;
	u8 MBXout;
	const struct _objd *objd;
	struct _COEsdo *coeres;
	u32 size;
	u8 dss;
	u32 abortcode = complete_access_get_variables(coesdo, &index, &subindex, &nidx, &nsub);

	if (abortcode != 0) {
		set_state_idle(0, index, subindex, abortcode);
		return;
	}

	MBXout = ESC_claimbuffer();

	if (MBXout == 0) {
		/* It is a bad idea to call SDO_abortwhen ESC_claimbufferfails,
		 * because SDO_abortwill also call ESC_claimbuffer...
		 */
		set_state_idle(0, index, subindex, 0);
		return;
	}

	objd = SDOobjects[nidx].objdesc;

	/* loop through the subindexes to get the total size */
	size = complete_access_subindex_loop(objd, nidx, nsub, NULL, UPLOAD, 0);

	/* expedited bits used calculation */
	dss = (size > 24) ? 0 : (u8)(4U * (3U - ((size - 1U) >> 3)));

	/* convert bits to bytes */
	size = BITS2BYTES(size);

	if (size > 0xffff) {
		/* 'size' is in this case actually an abort code */
		set_state_idle(MBXout, index, subindex, size);
		return;
	}

	/* check that upload data fits in the preallocated buffer */
	if ((size + PREALLOC_FACTOR * COE_HEADERSIZE) > PREALLOC_BUFFER_SIZE) {
		set_state_idle(MBXout, index, subindex, ABORT_CA_NOT_SUPPORTED);
		return;
	}
	abortcode = ESC_upload_pre_objecthandler(index,
						 subindex,
						 objd->data,
						 (size_t *)&size,
						 objd->flags | COMPLETE_ACCESS_FLAG);
	if (abortcode != 0) {
		set_state_idle(MBXout, index, subindex, abortcode);
		return;
	}

	/* copy subindex data into the preallocated buffer */
	complete_access_subindex_loop(objd, nidx, nsub, ESCvar.mbxdata, UPLOAD, 0);

	coeres = (struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];

	init_coesdo(coeres, COE_SDORESPONSE,
		    COE_COMMAND_UPLOADRESPONSE | COE_COMPLETEACCESS | COE_SIZE_INDICATOR,
		    index, subindex);

	ESCvar.segmented = 0;

	if (size <= 4) {
		/* expedited response, i.e. length <= 4 bytes */
		coeres->command |= (COE_EXPEDITED_INDICATOR | dss);
		memcpy(&coeres->size, ESCvar.mbxdata, size);
	} else {
		/* normal response, i.e. length > 4 bytes */
		coeres->size = htoel(size);

		if ((size + COE_HEADERSIZE) > ESC_MBXDSIZE) {
			/* segmented transfer needed */
			/* set total size in bytes */
			ESCvar.frags = size;
			/* limit to mailbox size */
			size = ESC_MBXDSIZE - COE_HEADERSIZE;
			/* number of bytes done */
			ESCvar.fragsleft = size;
			/* signal segmented transfer */
			ESCvar.segmented = MBXSEU;
			ESCvar.data = ESCvar.mbxdata;
			ESCvar.flags = COMPLETE_ACCESS_FLAG;
		}

		coeres->mbxheader.length = htoes(COE_HEADERSIZE + size);
		memcpy((&coeres->size) + 1, ESCvar.mbxdata, size);
	}

	if (ESCvar.segmented == 0) {
		abortcode = ESC_upload_post_objecthandler(index,
							  subindex,
							  objd->flags | COMPLETE_ACCESS_FLAG);

		if (abortcode != 0) {
			set_state_idle(MBXout, index, subindex, abortcode);
			return;
		}
	}

	MBXcontrol[MBXout].state = MBXstate_outreq;

	set_state_idle(MBXout, index, subindex, 0);
}

/** Function for handling the following SDO Upload if previous SDOUpload
 * response was flagged it needed to be segmented.
 */
static void SDO_uploadsegment(void)
{
	struct _COEsdo *coesdo, *coeres;
	u8 MBXout;
	u32 size, offset, abort;
	u8 command;

	coesdo = (struct _COEsdo *)&MBX[0];
	MBXout = ESC_claimbuffer();
	if (MBXout) {
		coeres = (struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];
		offset = ESCvar.fragsleft;
		size = ESCvar.frags - ESCvar.fragsleft;
		/* copy toggle bit */
		command = COE_COMMAND_UPLOADSEGMENT | (coesdo->command & COE_TOGGLEBIT);

		init_coesdo(coeres, COE_SDORESPONSE, command, coesdo->index, coesdo->subindex);
		if ((size + COE_SEGMENTHEADERSIZE) > ESC_MBXDSIZE) {
			/* more segmented transfer needed */
			/* limit to mailbox size */
			size = ESC_MBXDSIZE - COE_SEGMENTHEADERSIZE;
			/* number of bytes done */
			ESCvar.fragsleft += size;
			coeres->mbxheader.length = htoes(COE_SEGMENTHEADERSIZE + size);
		} else {
			/* last segment */
			ESCvar.segmented = 0;
			ESCvar.frags = 0;
			ESCvar.fragsleft = 0;
			coeres->command |= COE_COMMAND_LASTSEGMENTBIT;
			if (size >= 7) {
				coeres->mbxheader.length = htoes(COE_SEGMENTHEADERSIZE + size);
			} else {
				coeres->command |= (u8)((7U - size) << 1);
				coeres->mbxheader.length = htoes(COE_DEFAULTLENGTH);
			}
		}
		/* copy to mailbox */
		copy2mbx((u8 *)ESCvar.data + offset, (&coeres->command) + 1, size);

		if (ESCvar.segmented == 0) {
			abort = ESC_upload_post_objecthandler(etohs(coesdo->index),
							      coesdo->subindex,
							      ESCvar.flags);
			if (abort != 0) {
				set_state_idle(MBXout, etohs(coesdo->index),
					       coesdo->subindex,
					       abort);
				return;
			}
		}

		MBXcontrol[MBXout].state = MBXstate_outreq;
	}
	MBXcontrol[0].state = MBXstate_idle;
	ESCvar.xoe = 0;
}

/** Function for handling incoming requested SDO Download, validating the
 * request and sending an response. On error an SDO Abort will be sent.
 */
static void SDO_download(void)
{
	struct _COEsdo *coesdo, *coeres;
	u16 index;
	u8 subindex;
	s32 nidx;
	s16 nsub;
	u8 MBXout;
	u32 size, actsize;
	const struct _objd *objd;
	u32 *mbxdata;
	u32 abort;
	u8 access;
	u8 state;

	coesdo = (struct _COEsdo *)&MBX[0];
	index = etohs(coesdo->index);
	subindex = coesdo->subindex;
	nidx = SDO_findobject(index);
	if (nidx >= 0) {
		nsub = SDO_findsubindex(nidx, subindex);
		if (nsub >= 0) {
			objd = SDOobjects[nidx].objdesc;
			access = (objd + nsub)->flags & 0x3f;
			state = ESCvar.ALstatus & 0x0f;

			if (WRITE_ACCESS(access, state)) {
				/* expedited? */
				if (coesdo->command & COE_EXPEDITED_INDICATOR) {
					size = 4U - ((coesdo->command & 0x0CU) >> 2);
					mbxdata = &coesdo->size;
				} else {
					/* normal download */
					size = (etohl(coesdo->size) & 0xffff);
					mbxdata = (&coesdo->size) + 1;
				}
				actsize = BITS2BYTES((objd + nsub)->bitlength);
				if (actsize != size) {
					/* entries with data types VISIBLE_STRING, OCTET_STRING,
					 * UNICODE_STRING, ARRAY_OF_INT, ARRAY_OF_SINT,
					 * ARRAY_OF_DINT, and ARRAY_OF_UDINT may have flexible
					 * length
					 */
					u16 type = (objd + nsub)->datatype;

					if (type == DTYPE_VISIBLE_STRING) {
						/* pad with zeroes up to the maximum size of
						 * the entry
						 */
						memset((objd + nsub)->data + size, 0,
						       actsize - size);
					} else if ((type != DTYPE_OCTET_STRING) &&
						   (type != DTYPE_UNICODE_STRING) &&
						   (type != DTYPE_ARRAY_OF_INT) &&
						   (type != DTYPE_ARRAY_OF_SINT) &&
						   (type != DTYPE_ARRAY_OF_DINT) &&
						   (type != DTYPE_ARRAY_OF_UDINT)) {
						set_state_idle(0, index, subindex,
							       ABORT_TYPEMISMATCH);
						return;
					}
				}
				abort = ESC_download_pre_objecthandler(index,
								       subindex,
								       mbxdata,
								       size,
								       (objd + nsub)->flags);
				if (abort == 0) {
					if (size > 4 &&
					    (size > (coesdo->mbxheader.length - COE_HEADERSIZE))) {
						size = coesdo->mbxheader.length - COE_HEADERSIZE;
						/* signal segmented transfer */
						ESCvar.segmented = MBXSED;
						ESCvar.data = (objd + nsub)->data + size;
						ESCvar.index = index;
						ESCvar.subindex = subindex;
						ESCvar.flags = (objd + nsub)->flags;
					} else {
						ESCvar.segmented = 0;
					}
					copy2mbx(mbxdata, (objd + nsub)->data, size);
					MBXout = ESC_claimbuffer();
					if (MBXout) {
						coeres =
						(struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];
						coeres->mbxheader.length = htoes(COE_DEFAULTLENGTH);
						coeres->mbxheader.mbxtype = MBXCOE;
						coeres->coeheader.numberservice =
							htoes((0 & 0x01f) |
							      (COE_SDORESPONSE << 12));
						coeres->index = htoes(index);
						coeres->subindex = subindex;
						coeres->command = COE_COMMAND_DOWNLOADRESPONSE;
						coeres->size = htoel(0);
						MBXcontrol[MBXout].state = MBXstate_outreq;
					}
					if (ESCvar.segmented == 0) {
						/* external object write handler */
						abort =
						ESC_download_post_objecthandler(index,
										subindex,
										(objd + nsub)
										->flags);
						if (abort != 0)
							SDO_abort(MBXout, index, subindex, abort);
					}
				} else {
					SDO_abort(0, index, subindex, abort);
				}
			} else {
				if (access == ATYPE_RO)
					SDO_abort(0, index, subindex, ABORT_READONLY);
				else
					SDO_abort(0, index, subindex, ABORT_NOTINTHISSTATE);
			}
		} else {
			SDO_abort(0, index, subindex, ABORT_NOSUBINDEX);
		}
	} else {
		SDO_abort(0, index, subindex, ABORT_NOOBJECT);
	}
	MBXcontrol[0].state = MBXstate_idle;
	ESCvar.xoe = 0;
}

/** Function for handling incoming requested SDO Download with Complete Access,
 *  validating the request and sending a response. On error an SDO Abort will
 *  be sent.
 */
static void SDO_download_complete_access(void)
{
	struct _COEsdo *coesdo = (struct _COEsdo *)&MBX[0];
	u16 index;
	u8 subindex;
	s32 nidx;
	s16 nsub;
	u32 bytes;
	u32 *mbxdata;
	const struct _objd *objd;
	u32 size;
	u8 MBXout;
	u32 abortcode = complete_access_get_variables(coesdo, &index, &subindex, &nidx, &nsub);

	if (abortcode != 0) {
		set_state_idle(0, index, subindex, abortcode);
		return;
	}

	mbxdata = &coesdo->size;

	if (coesdo->command & COE_EXPEDITED_INDICATOR) {
		/* expedited download */
		bytes = 4U - ((coesdo->command & 0x0CU) >> 2);
	} else {
		/* normal download */
		bytes = (etohl(coesdo->size) & 0xffff);
		mbxdata++;
	}

	objd = SDOobjects[nidx].objdesc;

	/* loop through the subindexes to get the total size */
	size = complete_access_subindex_loop(objd, nidx, nsub, NULL, DOWNLOAD, 0);

	size = BITS2BYTES(size);
	if (size > 0xffff) {
		/* 'size' is in this case actually an abort code */
		set_state_idle(0, index, subindex, size);
		return;
	}
	/* The document ETG.1020 S (R) V1.3.0, chapter 12.2, states that
	 * "The SDO Download Complete Access data length shall always match
	 * the full current object size (defined by SubIndex0)".
	 * But EtherCAT Conformance Test Tool doesn't follow this rule for some test
	 * cases, which is the reason to here only check for 'less than or equal'.
	 */
	else if (bytes <= size) {
		abortcode = ESC_download_pre_objecthandler(index,
							   subindex,
							   mbxdata,
							   size,
							   objd->flags | COMPLETE_ACCESS_FLAG);
		if (abortcode != 0) {
			set_state_idle(0, index, subindex, abortcode);
			return;
		}

		if ((bytes + COE_HEADERSIZE) > ESC_MBXDSIZE) {
			/* check that download data fits in the preallocated buffer */
			if ((bytes + PREALLOC_FACTOR * COE_HEADERSIZE) > PREALLOC_BUFFER_SIZE) {
				set_state_idle(0, index, subindex, ABORT_CA_NOT_SUPPORTED);
				return;
			}
			/* set total size in bytes */
			ESCvar.frags = bytes;
			/* limit to mailbox size */
			size = ESC_MBXDSIZE - COE_HEADERSIZE;
			/* number of bytes done */
			ESCvar.fragsleft = size;
			ESCvar.segmented = MBXSED;
			ESCvar.data = ESCvar.mbxdata + size;
			ESCvar.index = index;
			ESCvar.subindex = subindex;
			ESCvar.flags = COMPLETE_ACCESS_FLAG;
			/* Store the data */
			copy2mbx(mbxdata, ESCvar.mbxdata, size);
		} else {
			ESCvar.segmented = 0;
			/* copy download data to subindexes */
			complete_access_subindex_loop(objd,
						      nidx,
						      nsub,
						      (u8 *)mbxdata,
						      DOWNLOAD,
						      bytes);

			abortcode = ESC_download_post_objecthandler(index,
								    subindex,
								    objd->flags |
								    COMPLETE_ACCESS_FLAG);
			if (abortcode != 0) {
				set_state_idle(0, index, subindex, abortcode);
				return;
			}
		}
	} else {
		set_state_idle(0, index, subindex, ABORT_TYPEMISMATCH);
		return;
	}

	MBXout = ESC_claimbuffer();

	if (MBXout > 0) {
		struct _COEsdo *coeres = (struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];

		init_coesdo(coeres, COE_SDORESPONSE,
			    COE_COMMAND_DOWNLOADRESPONSE | COE_COMPLETEACCESS,
			    index, subindex);

		coeres->size = 0;
		MBXcontrol[MBXout].state = MBXstate_outreq;
	}

	set_state_idle(MBXout, index, subindex, 0);
}

static void SDO_downloadsegment(void)
{
	u8 command;
	u8 command2;
	void *mbxdata;
	const struct _objd *objd;
	u32 abort;
	struct _COEsdo *coesdo = (struct _COEsdo *)&MBX[0];
	u8 MBXout = ESC_claimbuffer();

	if (MBXout) {
		struct _COEsdo *coeres = (struct _COEsdo *)&MBX[MBXout * ESC_MBXSIZE];
		u32 size = coesdo->mbxheader.length - 3U;

		if (size == 7)
			size = 7 - ((coesdo->command >> 1) & 7);

		command = COE_COMMAND_DOWNLOADSEGRESP;
		command2 = (coesdo->command & COE_TOGGLEBIT);  /* copy toggle bit */

		command |= command2;
		init_coesdo(coeres, COE_SDORESPONSE, command, 0, 0);

		mbxdata = &coesdo->index;  /* data pointer */

		copy2mbx(mbxdata, ESCvar.data, size);

		if (coesdo->command & COE_COMMAND_LASTSEGMENTBIT) {
			if (ESCvar.flags == COMPLETE_ACCESS_FLAG) {
				s32 nidx;
				s16 nsub;

				if (ESCvar.frags > ESCvar.fragsleft + size) {
					set_state_idle(0,
						       ESCvar.index,
						       ESCvar.subindex,
						       ABORT_TYPEMISMATCH);
					return;
				}

				nidx = SDO_findobject(ESCvar.index);
				nsub = SDO_findsubindex(nidx, ESCvar.subindex);

				if (nidx < 0 || nsub < 0) {
					set_state_idle(0,
						       ESCvar.index,
						       ESCvar.subindex,
						       ABORT_NOOBJECT);
					return;
				}

				/* copy download data to subindexes */
				objd = SDOobjects[nidx].objdesc;

				complete_access_subindex_loop(objd,
							      nidx,
							      nsub,
							      (u8 *)ESCvar.mbxdata,
							      DOWNLOAD,
							      ESCvar.frags);
			}
			/* last segment */
			ESCvar.segmented = 0;
			ESCvar.frags = 0;
			ESCvar.fragsleft = 0;
			/* external object write handler */
			abort = ESC_download_post_objecthandler(ESCvar.index,
								ESCvar.subindex,
								ESCvar.flags);
			if (abort != 0) {
				set_state_idle(MBXout, ESCvar.index, ESCvar.subindex, abort);
				return;
			}
		} else {
			/* more segmented transfer needed: increase offset */
			ESCvar.data += size;
			/* number of bytes done */
			ESCvar.fragsleft += size;
		}

		MBXcontrol[MBXout].state = MBXstate_outreq;
	}

	set_state_idle(0, 0, 0, 0);
}

/** Function for sending an SDO Info Error reply.
 *
 * @param[in] abortcode  = = abort code to send in reply
 */
static void SDO_infoerror(u32 abortcode)
{
	u8 MBXout;
	struct _COEobjdesc *coeres;

	MBXout = ESC_claimbuffer();
	if (MBXout) {
		coeres = (struct _COEobjdesc *)&MBX[MBXout * ESC_MBXSIZE];
		coeres->mbxheader.length = htoes(COE_HEADERSIZE);
		coeres->mbxheader.mbxtype = MBXCOE;
		coeres->coeheader.numberservice =
				htoes((0 & 0x01f) | (COE_SDOINFORMATION << 12));
		/* SDO info error request */
		coeres->infoheader.opcode = COE_INFOERROR;
		coeres->infoheader.incomplete = 0;
		coeres->infoheader.reserved = 0x00;
		coeres->infoheader.fragmentsleft = 0;
		coeres->index = (u16)htoel(abortcode);
		coeres->datatype = (u16)(htoel(abortcode) >> 16);
		MBXcontrol[MBXout].state = MBXstate_outreq;
		MBXcontrol[0].state = MBXstate_idle;
		ESCvar.xoe = 0;
	}
}

#define ODLISTSIZE \
	((u32)(ESC_MBX1_sml - ESC_MBXHSIZE - sizeof(struct _COEh) - sizeof(struct _INFOh) - 2U) & \
	 0xfffe)

/** Function for handling incoming requested SDO Get OD List, validating the
 * request and sending an response. On error an SDO Info Error will be sent.
 */
static void SDO_getodlist(void)
{
	u32 frags;
	u8 MBXout = 0;
	u16 entries = 0;
	u16 i, n;
	u16 *p;
	struct _COEobjdesc *coel, *coer;

	while (SDOobjects[entries].index != 0xffff)
		entries++;

	ESCvar.entries = entries;
	frags = ((u32)(entries << 1) + ODLISTSIZE - 1U);
	frags /= ODLISTSIZE;
	coer = (struct _COEobjdesc *)&MBX[0];
	/* check for unsupported opcodes */
	if (etohs(coer->index) > 0x01)
		SDO_infoerror(ABORT_UNSUPPORTED);
	else
		MBXout = ESC_claimbuffer();

	if (MBXout) {
		coel = (struct _COEobjdesc *)&MBX[MBXout * ESC_MBXSIZE];
		coel->mbxheader.mbxtype = MBXCOE;
		coel->coeheader.numberservice =
				htoes((0 & 0x01f) | (COE_SDOINFORMATION << 12));
		coel->infoheader.opcode = COE_GETODLISTRESPONSE;
		/* number of objects request */
		if (etohs(coer->index) == 0x00) {
			coel->index = htoes(0x00);
			coel->infoheader.incomplete = 0;
			coel->infoheader.reserved = 0x00;
			coel->infoheader.fragmentsleft = htoes(0);
			MBXcontrol[0].state = MBXstate_idle;
			ESCvar.xoe = 0;
			ESCvar.frags = frags;
			ESCvar.fragsleft = frags - 1;
			p = &coel->datatype;
			*p = htoes(entries);
			p++;
			*p = 0;
			p++;
			*p = 0;
			p++;
			*p = 0;
			p++;
			*p = 0;
			coel->mbxheader.length = htoes(0x08 + (5 << 1));
		}
		/* only return all objects */
		if (etohs(coer->index) == 0x01) {
			if (frags > 1) {
				coel->infoheader.incomplete = 1;
				ESCvar.xoe = MBXCOE + MBXODL;
				n = ODLISTSIZE >> 1;
			} else {
				coel->infoheader.incomplete = 0;
				MBXcontrol[0].state = MBXstate_idle;
				ESCvar.xoe = 0;
				n = entries;
			}
			coel->infoheader.reserved = 0x00;
			ESCvar.frags = frags;
			ESCvar.fragsleft = frags - 1;
			coel->infoheader.fragmentsleft = htoes(ESCvar.fragsleft);
			coel->index = htoes(0x01);

			p = &coel->datatype;
			for (i = 0; i < n; i++) {
				*p = htoes(SDOobjects[i].index);
				p++;
			}

			coel->mbxheader.length = htoes(0x08 + (n << 1));
		}
		MBXcontrol[MBXout].state = MBXstate_outreq;
	}
}

/** Function for continuing sending left overs from previous requested
 * SDO Get OD List, validating the request and sending an response.
 */
static void SDO_getodlistcont(void)
{
	u8 MBXout;
	u16 i, n, s;
	u16 *p;
	struct _COEobjdesc *coel;

	MBXout = ESC_claimbuffer();
	if (MBXout) {
		coel = (struct _COEobjdesc *)&MBX[MBXout * ESC_MBXSIZE];
		coel->mbxheader.mbxtype = MBXCOE;
		coel->coeheader.numberservice = htoes(COE_SDOINFORMATION << 12);
		coel->infoheader.opcode = COE_GETODLISTRESPONSE;
		s = (u16)((ESCvar.frags - ESCvar.fragsleft) * (ODLISTSIZE >> 1));
		if (ESCvar.fragsleft > 1) {
			coel->infoheader.incomplete = 1;
			n = (u16)(s + (ODLISTSIZE >> 1));
		} else {
			coel->infoheader.incomplete = 0;
			MBXcontrol[0].state = MBXstate_idle;
			ESCvar.xoe = 0;
			n = ESCvar.entries;
		}
		coel->infoheader.reserved = 0x00;
		ESCvar.fragsleft--;
		coel->infoheader.fragmentsleft = htoes((u16)ESCvar.fragsleft);
		/* pointer 2 bytes back to exclude index */
		p = &coel->index;
		for (i = s; i < n; i++) {
			*p = htoes(SDOobjects[i].index);
			p++;
		}
		coel->mbxheader.length = htoes(0x06 + ((n - s) << 1));
		MBXcontrol[MBXout].state = MBXstate_outreq;
	}
}

/** Function for handling incoming requested SDO Get Object Description,
 * validating the request and sending an response. On error an
 * SDO Info Error will be sent.
 */
static void SDO_getod(void)
{
	u8 MBXout;
	u16 index;
	s32 nidx;
	u8 *d;
	const u8 *s;
	u8 n = 0;
	struct _COEobjdesc *coer, *coel;

	coer = (struct _COEobjdesc *)&MBX[0];
	index = etohs(coer->index);
	nidx = SDO_findobject(index);
	if (nidx >= 0) {
		MBXout = ESC_claimbuffer();
		if (MBXout) {
			coel = (struct _COEobjdesc *)&MBX[MBXout * ESC_MBXSIZE];
			coel->mbxheader.mbxtype = MBXCOE;
			coel->coeheader.numberservice = htoes(COE_SDOINFORMATION << 12);
			coel->infoheader.opcode = COE_GETODRESPONSE;
			coel->infoheader.incomplete = 0;
			coel->infoheader.reserved = 0x00;
			coel->infoheader.fragmentsleft = htoes(0);
			coel->index = htoes(index);
			if (SDOobjects[nidx].objtype == OTYPE_VAR) {
				s32 nsub = SDO_findsubindex(nidx, 0);
				const struct _objd *objd = SDOobjects[nidx].objdesc;

				coel->datatype = htoes((objd + nsub)->datatype);
				coel->maxsub = SDOobjects[nidx].maxsub;
			} else if (SDOobjects[nidx].objtype == OTYPE_ARRAY) {
				s32 nsub = SDO_findsubindex(nidx, 0);
				const struct _objd *objd = SDOobjects[nidx].objdesc;

				coel->datatype = htoes((objd + nsub)->datatype);
				coel->maxsub = (u8)SDOobjects[nidx].objdesc->value;
			} else {
				coel->datatype = htoes(0);
				coel->maxsub = (u8)SDOobjects[nidx].objdesc->value;
			}
			coel->objectcode = (u8)SDOobjects[nidx].objtype;
			s = (u8 *)SDOobjects[nidx].name;
			d = (u8 *)&coel->name;
			while (*s && (n < (ESC_MBXDSIZE - 0x0c))) {
				*d = *s;
				n++;
				s++;
				d++;
			}
			*d = *s;
			coel->mbxheader.length = htoes(0x0C + n);
			MBXcontrol[MBXout].state = MBXstate_outreq;
			MBXcontrol[0].state = MBXstate_idle;
			ESCvar.xoe = 0;
		}
	} else {
		SDO_infoerror(ABORT_NOOBJECT);
	}
}

/** Function for handling incoming requested SDO Get Entry Description,
 * validating the request and sending an response. On error an
 * SDO Info Error will be sent.
 */
static void SDO_geted(void)
{
	u8 MBXout;
	u16 index;
	s32 nidx;
	s16 nsub;
	u8 subindex;
	u8 *d;
	const u8 *s;
	const struct _objd *objd;
	u8 n = 0;
	struct _COEentdesc *coer, *coel;

	coer = (struct _COEentdesc *)&MBX[0];
	index = etohs(coer->index);
	subindex = coer->subindex;
	nidx = SDO_findobject(index);
	if (nidx >= 0) {
		nsub = SDO_findsubindex(nidx, subindex);
		if (nsub >= 0) {
			objd = SDOobjects[nidx].objdesc;
			MBXout = ESC_claimbuffer();
			if (MBXout) {
				coel = (struct _COEentdesc *)&MBX[MBXout * ESC_MBXSIZE];
				coel->mbxheader.mbxtype = MBXCOE;
				coel->coeheader.numberservice =
						htoes((0 & 0x01f) | (COE_SDOINFORMATION << 12));
				coel->infoheader.opcode = COE_ENTRYDESCRIPTIONRESPONSE;
				coel->infoheader.incomplete = 0;
				coel->infoheader.reserved = 0x00;
				coel->infoheader.fragmentsleft = htoes(0);
				coel->index = htoes(index);
				coel->subindex = subindex;
				coel->valueinfo = COE_VALUEINFO_ACCESS +
						  COE_VALUEINFO_OBJECT +
						  COE_VALUEINFO_MAPPABLE;
				coel->datatype = htoes((objd + nsub)->datatype);
				coel->bitlength = htoes((objd + nsub)->bitlength);
				coel->access = htoes((objd + nsub)->flags);
				s = (u8 *)(objd + nsub)->name;
				d = (u8 *)&coel->name;
				while (*s && (n < (ESC_MBXDSIZE - 0x10))) {
					*d = *s;
					n++;
					s++;
					d++;
				}
				*d = *s;
				coel->mbxheader.length = htoes(0x10 + n);
				MBXcontrol[MBXout].state = MBXstate_outreq;
				MBXcontrol[0].state = MBXstate_idle;
				ESCvar.xoe = 0;
			}
		} else {
			SDO_infoerror(ABORT_NOSUBINDEX);
		}
	} else {
		SDO_infoerror(ABORT_NOOBJECT);
	}
}

/** Main CoE function checking the status on current mailbox buffers carrying
 * data, distributing the mailboxes to appropriate CoE functions.
 * On Error an MBX_erroror SDO Abort will be sent depending on error cause.
 */
void ESC_coeprocess(void)
{
	struct _MBXh *mbh;
	struct _COEsdo *coesdo;
	struct _COEobjdesc *coeobjdesc;
	u16 service;

	if (ESCvar.MBXrun == 0)
		return;

	if (!ESCvar.xoe && MBXcontrol[0].state == MBXstate_inclaim) {
		mbh = (struct _MBXh *)&MBX[0];
		if (mbh->mbxtype == MBXCOE) {
			if (etohs(mbh->length) < COE_MINIMUM_LENGTH)
				MBX_error(MBXERR_INVALIDSIZE);
			else
				ESCvar.xoe = MBXCOE;
		}
	}

	if ((ESCvar.xoe == (MBXCOE + MBXODL)) && !ESCvar.mbxoutpost)
		/* continue get OD list */
		SDO_getodlistcont();

	if (ESCvar.xoe == MBXCOE) {
		coesdo = (struct _COEsdo *)&MBX[0];
		coeobjdesc = (struct _COEobjdesc *)&MBX[0];
		service = etohs(coesdo->coeheader.numberservice) >> 12;
		if (service == COE_SDOREQUEST) {
			if ((SDO_COMMAND(coesdo->command) == COE_COMMAND_UPLOADREQUEST) &&
			    (etohs(coesdo->mbxheader.length) == COE_HEADERSIZE)) {
			/* initiate SDO upload request */
				if (SDO_COMPLETE_ACCESS(coesdo->command))
					SDO_upload_complete_access();
				else
					SDO_upload();
			} else if (((coesdo->command & 0xef) == COE_COMMAND_UPLOADSEGREQ) &&
				   (etohs(coesdo->mbxheader.length) == COE_HEADERSIZE) &&
				   (ESCvar.segmented == MBXSEU)) {
				/* SDO upload segment request */
				SDO_uploadsegment();
			} else if (SDO_COMMAND(coesdo->command) == COE_COMMAND_DOWNLOADREQUEST) {
				/* initiate SDO download request */
				if (SDO_COMPLETE_ACCESS(coesdo->command))
					SDO_download_complete_access();
				else
					SDO_download();
			} else if (SDO_COMMAND(coesdo->command) == COE_COMMAND_DOWNLOADSEGREQ) {
				/* SDO download segment request */
				SDO_downloadsegment();
			}
		} else {
			/* initiate SDO get OD list */
			if (service == COE_SDOINFORMATION &&
			    coeobjdesc->infoheader.opcode == 0x01) {
				SDO_getodlist();
			} else {
				/* initiate SDO get OD */
				if (service == COE_SDOINFORMATION &&
				    coeobjdesc->infoheader.opcode == 0x03) {
					SDO_getod();
				} else {
					/* initiate SDO get ED */
					if (service == COE_SDOINFORMATION &&
					    coeobjdesc->infoheader.opcode == 0x05) {
						SDO_geted();
					} else {
						/* COE not recognised above */
						if (ESCvar.xoe == MBXCOE) {
							if (service == 0)
								MBX_error(MBXERR_INVALIDHEADER);
							else
								SDO_abort(0,
									  etohs(coesdo->index),
									  coesdo->subindex,
									  ABORT_UNSUPPORTED);

							MBXcontrol[0].state = MBXstate_idle;
							ESCvar.xoe = 0;
						}
					}
				}
			}
		}
	}
}

/**
 * Get value from bitmap
 *
 * This function gets a value from a bitmap.
 *
 * @param[in] bitmap = bitmap containing value
 * @param[in] offset = start offset
 * @param[in] length = number of bits to get
 * @return bitslice value
 */
static u64 COE_bitsliceGet(u64 *bitmap, unsigned int offset, unsigned int length)
{
	const unsigned int word_offset = offset / 64;
	const unsigned int bit_offset = offset % 64;
	const u64 mask = (length == 64) ? U64_MAX : (1ULL << length) - 1;
	u64 w0;
	u64 w1 = 0;

	/* Get the least significant word */
	w0 = bitmap[word_offset];
	w0 = w0 >> bit_offset;

	/* Get the most significant word, if required */
	if (length + bit_offset > 64) {
		w1 = bitmap[word_offset + 1];
		w1 = w1 << (64 - bit_offset);
	}

	w0 = (w1 | w0);
	return (w0 & mask);
}

/**
 * Set value in bitmap
 *
 * This function sets a value in a bitmap.
 *
 * @param[in] bitmap = bitmap to contain value
 * @param[in] offset = start offset
 * @param[in] length = number of bits to set
 * @param[in] value  = value to set
 */
static void COE_bitsliceSet(u64 *bitmap, unsigned int offset, unsigned int length,
			    u64 value)
{
	const unsigned int word_offset = offset / 64;
	const unsigned int bit_offset = offset % 64;
	const u64 mask = (length == 64) ? U64_MAX : (1ULL << length) - 1;
	const u64 mask0 = mask << bit_offset;
	u64 v0 = value << bit_offset;
	u64 w0 = bitmap[word_offset];

	/* Set the least significant word */
	w0 = (w0 & ~mask0) | (v0 & mask0);
	bitmap[word_offset] = w0;

	/* Set the most significant word, if required */
	if (length + bit_offset > 64) {
		const u64 mask1 = mask >> (64 - bit_offset);
		u64 v1 = value >> (64 - bit_offset);
		u64 w1 = bitmap[word_offset + 1];

		w1 = (w1 & ~mask1) | (v1 & mask1);
		bitmap[word_offset + 1] = w1;
	}
}

/**
 * Get object value
 *
 * This function atomically gets an object value.
 *
 * @param[in] obj   = object description
 * @return object value
 */
static u64 COE_getValue(const struct _objd *obj)
{
	u64 value = 0;

	/* TODO: const data */

	switch (obj->datatype) {
	case DTYPE_BIT1:
	case DTYPE_BIT2:
	case DTYPE_BIT3:
	case DTYPE_BIT4:
	case DTYPE_BIT5:
	case DTYPE_BIT6:
	case DTYPE_BIT7:
	case DTYPE_BIT8:
	case DTYPE_BOOLEAN:
	case DTYPE_UNSIGNED8:
	case DTYPE_INTEGER8:
	case DTYPE_BITARR8:
		value = *(u8 *)obj->data;
		break;
	case DTYPE_UNSIGNED16:
	case DTYPE_INTEGER16:
	case DTYPE_BITARR16:
		value = *(u16 *)obj->data;
		break;
	case DTYPE_REAL32:
	case DTYPE_UNSIGNED32:
	case DTYPE_INTEGER32:
	case DTYPE_BITARR32:
		value = *(u32 *)obj->data;
		break;
	case DTYPE_REAL64:
	case DTYPE_UNSIGNED64:
	case DTYPE_INTEGER64:
		/* FIXME: must be atomic */
		value = *(u64 *)obj->data;
		break;
	default:
		CC_ASSERT(0);
	}

	return value;
}

/**
 * Set object value
 *
 * This function atomically sets an object value.
 *
 * @param[in] obj   = object description
 * @param[in] value = new value
 */
static void COE_setValue(const struct _objd *obj, u64 value)
{
	switch (obj->datatype) {
	case DTYPE_BIT1:
	case DTYPE_BIT2:
	case DTYPE_BIT3:
	case DTYPE_BIT4:
	case DTYPE_BIT5:
	case DTYPE_BIT6:
	case DTYPE_BIT7:
	case DTYPE_BIT8:
	case DTYPE_BOOLEAN:
	case DTYPE_UNSIGNED8:
	case DTYPE_INTEGER8:
	case DTYPE_BITARR8:
		*(u8 *)obj->data = value & U8_MAX;
		break;
	case DTYPE_UNSIGNED16:
	case DTYPE_INTEGER16:
	case DTYPE_BITARR16:
		*(u16 *)obj->data = value & U16_MAX;
		break;
	case DTYPE_REAL32:
	case DTYPE_UNSIGNED32:
	case DTYPE_INTEGER32:
	case DTYPE_BITARR32:
		*(u32 *)obj->data = value & U32_MAX;
		break;
	case DTYPE_REAL64:
	case DTYPE_UNSIGNED64:
	case DTYPE_INTEGER64:
		/* FIXME: must be atomic */
		*(u64 *)obj->data = value;
		break;
	default:
		DPRINT("ignored\n");
		break;
	}
}

/**
 * Init default values for SDO objects
 */
void COE_initDefaultValues(void)
{
	int i;
	const struct _objd *objd;
	int n;
	u8 maxsub;

	/* Let application decide if initialization will be skipped */
	if (ESCvar.skip_default_initialization)
		return;

	/* Set default values from object descriptor */
	for (n = 0; SDOobjects[n].index != 0xffff; n++) {
		objd = SDOobjects[n].objdesc;
		maxsub = SDOobjects[n].maxsub;

		i = 0;
		do {
			if (objd[i].data) {
				COE_setValue(&objd[i], objd[i].value);
				DPRINT("%04" PRIx32 ":%02" PRIx32 " = %" PRIx32 "\n",
				       SDOobjects[n].index,
				       objd[i].subindex,
				       objd[i].value);
			}
		} while (objd[i++].subindex < maxsub);
	}

	/* Let application override default values */
	if (ESCvar.set_defaults_hook)
		ESCvar.set_defaults_hook();
}

/**
 * Pack process data
 *
 * This function reads mapped objects and constructs the process data
 * inputs (TXPDO).
 *
 * @param[in] buffer     = input process data
 * @param[in] nmappings  = number of mappings in sync manager
 * @param[in] mappings   = list of mapped objects in sync manager
 */
void COE_pdoPack(u8 *buffer, int nmappings, struct _SMmap *mappings)
{
	int ix;

	/* Check that buffer is aligned on 64-bit boundary */
	CC_ASSERT(((uintptr_t)buffer & 0x07) == 0);

	for (ix = 0; ix < nmappings; ix++) {
		const struct _objd *obj = mappings[ix].obj;
		u32 offset = mappings[ix].offset;

		if (obj) {
			if (obj->bitlength > 64) {
				memcpy(&buffer[BITSPOS2BYTESOFFSET(offset)],
				       obj->data,
				       BITS2BYTES(obj->bitlength));
			} else {
				/* Atomically get object value */
				u64 value = COE_getValue(obj);

				COE_bitsliceSet((u64 *)buffer, offset, obj->bitlength, value);
			}
		}
	}
}

/**
 * Unpack process data
 *
 * This function unpacks process data output (RXPDO) and writes to the
 * mapped objects.
 *
 * @param[in] buffer    = output process data
 * @param[in] nmappings = number of mappings in sync manager
 * @param[in] mappings  = list of mapped objects in sync manager
 */
void COE_pdoUnpack(u8 *buffer, int nmappings, struct _SMmap *mappings)
{
	int ix;

	/* Check that buffer is aligned on 64-bit boundary */
	CC_ASSERT(((uintptr_t)buffer & 0x07) == 0);

	for (ix = 0; ix < nmappings; ix++) {
		const struct _objd *obj = mappings[ix].obj;
		u32 offset = mappings[ix].offset;

		if (obj) {
			if (obj->bitlength > 64) {
				memcpy(obj->data,
				       &buffer[BITSPOS2BYTESOFFSET(offset)],
				       BITS2BYTES(obj->bitlength));
			} else {
				/* Atomically set object value */
				u64 value = COE_bitsliceGet((u64 *)buffer, offset, obj->bitlength);

				COE_setValue(obj, value);
			}
		}
	}
}

/**
 * Fetch max subindex
 *
 * This function fetches the value of subindex 0 (max subindex).
 *
 * @param[in] index = object index
 */
u8 COE_maxSub(u16 index)
{
	s32 nidx;
	u8 maxsub;

	nidx = SDO_findobject(index);
	if (nidx == -1)
		return 0;

	maxsub = OBJ_VALUE_FETCH(maxsub, SDOobjects[nidx].objdesc[0]);
	return maxsub;
}
