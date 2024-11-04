/*
 *Licensed under the GNU General Public License version 2 with exceptions. See
 *LICENSE file in the project root for full license information
 */

/** \file
 *\brief
 *Headerfile for esc.h
 */

#ifndef __esc__
#define __esc__

#include "include/sys/gcc/cc.h"
#include "esc_coe.h"
#include "options.h"

#define ESCREG_ADDRESS              0x0010
#define ESCREG_CONF_STATION_ALIAS   0x0012
#define ESCREG_DLSTATUS             0x0110
#define ESCREG_ALCONTROL            0x0120
#define ESCREG_ALCONTROL_ERROR_ACK  0x0010
#define ESCREG_ALSTATUS             0x0130
#define ESCREG_ALSTATUS_ERROR_IND   0x0010
#define ESCREG_ALERROR              0x0134
#define ESCREG_ALEVENTMASK          0x0204
#define ESCREG_ALEVENT              0x0220
#define ESCREG_ALEVENT_SM_MASK      0x0310
#define ESCREG_ALEVENT_SMCHANGE     0x0010
#define ESCREG_ALEVENT_CONTROL      0x0001
#define ESCREG_ALEVENT_DC_LATCH     0x0002
#define ESCREG_ALEVENT_DC_SYNC0     0x0004
#define ESCREG_ALEVENT_DC_SYNC1     0x0008
#define ESCREG_ALEVENT_EEP          0x0020
#define ESCREG_ALEVENT_WD           0x0040
#define ESCREG_ALEVENT_SM0          0x0100
#define ESCREG_ALEVENT_SM1          0x0200
#define ESCREG_ALEVENT_SM2          0x0400
#define ESCREG_ALEVENT_SM3          0x0800
#define ESCREG_WDSTATUS             0x0440
#define ESCREG_EECONTSTAT           0x0502
#define ESCREG_EEDATA               0x0508
#define ESCREG_SM0                  0x0800
#define ESCREG_SM0STATUS            (ESCREG_SM0 + 5)
#define ESCREG_SM0ACTIVATE          (ESCREG_SM0 + 6)
#define ESCREG_SM0PDI               (ESCREG_SM0 + 7)
#define ESCREG_SM1                  (ESCREG_SM0 + 0x08)
#define ESCREG_SM2                  (ESCREG_SM0 + 0x10)
#define ESCREG_SM3                  (ESCREG_SM0 + 0x18)
#define ESCREG_LOCALTIME            0x0910
#define ESCREG_LOCALTIME_OFFSET     0x0920
#define ESCREG_SYNC_ACT             0x0981
#define ESCREG_SYNC_ACT_ACTIVATED   0x01
#define ESCREG_SYNC_SYNC0_EN        0x02
#define ESCREG_SYNC_SYNC1_EN        0x04
#define ESCREG_SYNC_AUTO_ACTIVATED  0x08
#define ESCREG_SYNC0_CYCLE_TIME     0x09A0
#define ESCREG_SYNC1_CYCLE_TIME     0x09A4
#define ESCREG_SMENABLE_BIT         0x01
#define ESCREG_AL_STATEMASK         0x001f
#define ESCREG_AL_ALLBUTINITMASK    0x0e
#define ESCREG_AL_ERRACKMASK        0x0f
#define ESCREG_AL_ID_REQUEST        0x0020

#define SYNCTYPE_SUPPORT_FREERUN    0x01
#define SYNCTYPE_SUPPORT_SYNCHRON   0x02
#define SYNCTYPE_SUPPORT_DCSYNC0    0x04
#define SYNCTYPE_SUPPORT_DCSYNC1    0x08
#define SYNCTYPE_SUPPORT_SUBCYCLE   0x10

#define ESCinit                  0x01
#define ESCpreop                 0x02
#define ESCboot                  0x03
#define ESCsafeop                0x04
#define ESCop                    0x08
#define ESCerror                 0x10

#define INIT_TO_INIT             0x11
#define INIT_TO_PREOP            0x21
#define INIT_TO_BOOT             0x31
#define INIT_TO_SAFEOP           0x41
#define INIT_TO_OP               0x81
#define PREOP_TO_INIT            0x12
#define PREOP_TO_PREOP           0x22
#define PREOP_TO_BOOT            0x32
#define PREOP_TO_SAFEOP          0x42
#define PREOP_TO_OP              0x82
#define BOOT_TO_INIT             0x13
#define BOOT_TO_PREOP            0x23
#define BOOT_TO_BOOT             0x33
#define BOOT_TO_SAFEOP           0x43
#define BOOT_TO_OP               0x83
#define SAFEOP_TO_INIT           0x14
#define SAFEOP_TO_PREOP          0x24
#define SAFEOP_TO_BOOT           0x34
#define SAFEOP_TO_SAFEOP         0x44
#define SAFEOP_TO_OP             0x84
#define OP_TO_INIT               0x18
#define OP_TO_PREOP              0x28
#define OP_TO_BOOT               0x38
#define OP_TO_SAFEOP             0x48
#define OP_TO_OP                 0x88

#define ALERR_NONE                  0x0000
#define ALERR_UNSPECIFIEDERROR      0x0001
#define ALERR_NOMEMORY              0x0002
#define ALERR_INVALIDSTATECHANGE    0x0011
#define ALERR_UNKNOWNSTATE          0x0012
#define ALERR_BOOTNOTSUPPORTED      0x0013
#define ALERR_NOVALIDFIRMWARE       0x0014
#define ALERR_INVALIDBOOTMBXCONFIG  0x0015
#define ALERR_INVALIDMBXCONFIG      0x0016
#define ALERR_INVALIDSMCONFIG       0x0017
#define ALERR_NOVALIDINPUTS         0x0018
#define ALERR_NOVALIDOUTPUTS        0x0019
#define ALERR_SYNCERROR             0x001A
#define ALERR_WATCHDOG              0x001B
#define ALERR_INVALIDSYNCMANAGERTYP 0x001C
#define ALERR_INVALIDOUTPUTSM       0x001D
#define ALERR_INVALIDINPUTSM        0x001E
#define ALERR_INVALIDWDTCFG         0x001F
#define ALERR_SLAVENEEDSCOLDSTART   0x0020
#define ALERR_SLAVENEEDSINIT        0x0021
#define ALERR_SLAVENEEDSPREOP       0x0022
#define ALERR_SLAVENEEDSSAFEOP      0x0023
#define ALERR_INVALIDINPUTMAPPING   0x0024
#define ALERR_INVALIDOUTPUTMAPPING  0x0025
#define ALERR_INCONSISTENTSETTINGS  0x0026
#define ALERR_FREERUNNOTSUPPORTED   0x0027
#define ALERR_SYNCNOTSUPPORTED      0x0028
#define ALERR_FREERUNNEEDS3BUFFMODE 0x0029
#define ALERR_BACKGROUNDWATCHDOG    0x002A
#define ALERR_NOVALIDINPUTSOUTPUTS  0x002B
#define ALERR_FATALSYNCERROR        0x002C
#define ALERR_NOSYNCERROR           0x002D
#define ALERR_INVALIDINPUTFMMUCFG   0x002E
#define ALERR_DCINVALIDSYNCCFG      0x0030
#define ALERR_INVALIDDCLATCHCFG     0x0031
#define ALERR_PLLERROR              0x0032
#define ALERR_DCSYNCIOERROR         0x0033
#define ALERR_DCSYNCTIMEOUT         0x0034
#define ALERR_DCSYNCCYCLETIME       0x0035
#define ALERR_DCSYNC0CYCLETIME      0x0036
#define ALERR_DCSYNC1CYCLETIME      0x0037
#define ALERR_MBXAOE                0x0041
#define ALERR_MBXEOE                0x0042
#define ALERR_MBXCOE                0x0043
#define ALERR_MBXFOE                0x0044
#define ALERR_MBXSOE                0x0045
#define ALERR_MBXVOE                0x004F
#define ALERR_EEPROMNOACCESS        0x0050
#define ALERR_EEPROMERROR           0x0051
#define ALERR_SLAVERESTARTEDLOCALLY 0x0060
#define ALERR_DEVICEIDVALUEUPDATED  0x0061
#define ALERR_APPLCTRLAVAILABLE     0x00f0
#define ALERR_UNKNOWN               0xffff

#define MBXERR_SYNTAX                   0x0001
#define MBXERR_UNSUPPORTEDPROTOCOL      0x0002
#define MBXERR_INVALIDCHANNEL           0x0003
#define MBXERR_SERVICENOTSUPPORTED      0x0004
#define MBXERR_INVALIDHEADER            0x0005
#define MBXERR_SIZETOOSHORT             0x0006
#define MBXERR_NOMOREMEMORY             0x0007
#define MBXERR_INVALIDSIZE              0x0008

#define ABORT_NOTOGGLE                  0x05030000
#define ABORT_TRANSFER_TIMEOUT          0x05040000
#define ABORT_UNKNOWN                   0x05040001
#define ABORT_INVALID_BLOCK_SIZE        0x05040002
#define ABORT_INVALID_SEQUENCE_NUMBER   0x05040003
#define ABORT_BLOCK_CRC_ERROR           0x05040004
#define ABORT_OUT_OF_MEMORY             0x05040005
#define ABORT_UNSUPPORTED               0x06010000
#define ABORT_WRITEONLY                 0x06010001
#define ABORT_READONLY                  0x06010002
#define ABORT_SUBINDEX0_NOT_ZERO        0x06010003
#define ABORT_CA_NOT_SUPPORTED          0x06010004
#define ABORT_EXCEEDS_MBOX_SIZE         0x06010005
#define ABORT_SDO_DOWNLOAD_BLOCKED      0x06010006
#define ABORT_NOOBJECT                  0x06020000
#define ABORT_MAPPING_OBJECT_ERROR      0x06040041
#define ABORT_MAPPING_LENGTH_ERROR      0x06040042
#define ABORT_GENERAL_PARAMETER_ERROR   0x06040043
#define ABORT_GENERAL_DEVICE_ERROR      0x06040047
#define ABORT_HARDWARE_ERROR            0x06060000
#define ABORT_TYPEMISMATCH              0x06070010
#define ABORT_DATATYPE_TOO_HIGH         0x06070012
#define ABORT_DATATYPE_TOO_LOW          0x06070013
#define ABORT_NOSUBINDEX                0x06090011
#define ABORT_VALUE_EXCEEDED            0x06090030
#define ABORT_VALUE_TOO_HIGH            0x06090031
#define ABORT_VALUE_TOO_LOW             0x06090032
#define ABORT_MODULE_LIST_MISMATCH      0x06090033
#define ABORT_MAX_VAL_LESS_THAN_MIN_VAL 0x06090036
#define ABORT_RESOURCE_NOT_AVAILABLE    0x060A0023
#define ABORT_GENERALERROR              0x08000000
#define ABORT_DATA_STORE_ERROR          0x08000020
#define ABORT_DATA_STORE_LOCAL_ERROR    0x08000021
#define ABORT_NOTINTHISSTATE            0x08000022
#define ABORT_OBJECT_DICTIONARY_ERROR   0x08000023
#define ABORT_NO_DATA_AVAILABLE         0x08000024

#define MBXstate_idle                   0x00
#define MBXstate_inclaim                0x01
#define MBXstate_outclaim               0x02
#define MBXstate_outreq                 0x03
#define MBXstate_outpost                0x04
#define MBXstate_backup                 0x05
#define MBXstate_again                  0x06

#define COE_DEFAULTLENGTH               0x0AU
#define COE_HEADERSIZE                  0x0AU
#define COE_SEGMENTHEADERSIZE           0x03U
#define COE_SDOREQUEST                  0x02
#define COE_SDORESPONSE                 0x03
#define COE_SDOINFORMATION              0x08
#define COE_COMMAND_SDOABORT            0x80
#define COE_COMMAND_UPLOADREQUEST       0x40
#define COE_COMMAND_UPLOADRESPONSE      0x40
#define COE_COMMAND_UPLOADSEGMENT       0x00
#define COE_COMMAND_UPLOADSEGREQ        0x60
#define COE_COMMAND_DOWNLOADREQUEST     0x20
#define COE_COMMAND_DOWNLOADRESPONSE    0x60
#define COE_COMMAND_DOWNLOADSEGREQ      0x00
#define COE_COMMAND_DOWNLOADSEGRESP     0x20
#define COE_COMMAND_LASTSEGMENTBIT      0x01
#define COE_SIZE_INDICATOR              0x01
#define COE_EXPEDITED_INDICATOR         0x02
#define COE_COMPLETEACCESS              0x10
#define COE_TOGGLEBIT                   0x10
#define COE_INFOERROR                   0x07
#define COE_GETODLISTRESPONSE           0x02
#define COE_GETODRESPONSE               0x04
#define COE_ENTRYDESCRIPTIONRESPONSE    0x06
#define COE_VALUEINFO_ACCESS            0x01
#define COE_VALUEINFO_OBJECT            0x02
#define COE_VALUEINFO_MAPPABLE          0x04
#define COE_VALUEINFO_TYPE              0x08
#define COE_VALUEINFO_DEFAULT           0x10
#define COE_VALUEINFO_MINIMUM           0x20
#define COE_VALUEINFO_MAXIMUM           0x40
#define COE_MINIMUM_LENGTH              8

#define MBXERR                         0x00
#define MBXAOE                         0x01
#define MBXEOE                         0x02
#define MBXCOE                         0x03
#define MBXFOE                         0x04
#define MBXODL                         0x10
#define MBXOD                          0x20
#define MBXED                          0x30
#define MBXSEU                         0x40
#define MBXSED                         0x50

#define SMRESULT_ERRSM0                0x01
#define SMRESULT_ERRSM1                0x02
#define SMRESULT_ERRSM2                0x04
#define SMRESULT_ERRSM3                0x08

#define FOE_ERR_NOTDEFINED             0x8000
#define FOE_ERR_NOTFOUND               0x8001
#define FOE_ERR_ACCESS                 0x8002
#define FOE_ERR_DISKFULL               0x8003
#define FOE_ERR_ILLEGAL                0x8004
#define FOE_ERR_PACKETNO               0x8005
#define FOE_ERR_EXISTS                 0x8006
#define FOE_ERR_NOUSER                 0x8007
#define FOE_ERR_BOOTSTRAPONLY          0x8008
#define FOE_ERR_NOTINBOOTSTRAP         0x8009
#define FOE_ERR_NORIGHTS               0x800A
#define FOE_ERR_PROGERROR              0x800B
#define FOE_ERR_CHECKSUM               0x800C

#define FOE_OP_RRQ                     1
#define FOE_OP_WRQ                     2
#define FOE_OP_DATA                    3
#define FOE_OP_ACK                     4
#define FOE_OP_ERR                     5
#define FOE_OP_BUSY                    6

#define FOE_READY                      0
#define FOE_WAIT_FOR_ACK               1
#define FOE_WAIT_FOR_FINAL_ACK         2
#define FOE_WAIT_FOR_DATA              3

#define EOE_RESULT_SUCCESS                   0x0000
#define EOE_RESULT_UNSPECIFIED_ERROR         0x0001
#define EOE_RESULT_UNSUPPORTED_FRAME_TYPE    0x0002
#define EOE_RESULT_NO_IP_SUPPORT             0x0201
#define EOE_RESULT_NO_DHCP_SUPPORT           0x0202
#define EOE_RESULT_NO_FILTER_SUPPORT         0x0401

#define APPSTATE_IDLE                  0x00
#define APPSTATE_INPUT                 0x01
#define APPSTATE_OUTPUT                0x02

#define PREALLOC_BUFFER_SIZE  (PREALLOC_FACTOR * MBXSIZE)

struct sm_cfg_t {
	u16 cfg_sma;
	u16 cfg_sml;
	u16 cfg_sme;
	u8 cfg_smc;
	u8 cfg_smact;
};

struct esc_cfg_t {
	void *user_arg;
	int use_interrupt;
	int watchdog_cnt;
	bool skip_default_initialization;
	void (*set_defaults_hook)(void);
	void (*pre_state_change_hook)(u8 *as, u8 *an);
	void (*post_state_change_hook)(u8 *as, u8 *an);
	void (*application_hook)(void);
	void (*safeoutput_override)(void);
	u32 (*pre_object_download_hook)(u16 index,
					u8 subindex,
					void *data,
					size_t size,
					u16 flags);
	u32 (*post_object_download_hook)(u16 index,
					 u8 subindex,
					 u16 flags);
	u32 (*pre_object_upload_hook)(u16 index,
				      u8 subindex,
				      void *data,
				      size_t *size,
				      u16 flags);
	u32 (*post_object_upload_hook)(u16 index,
				       u8 subindex,
				       u16 flags);
	void (*rxpdo_override)(void);
	void (*txpdo_override)(void);
	void (*esc_hw_interrupt_enable)(u32 mask);
	void (*esc_hw_interrupt_disable)(u32 mask);
	void (*esc_hw_eep_handler)(void);
	u16 (*esc_check_dc_handler)(void);
	int (*get_device_id)(u16 *device_id);
};

struct _App {
	u8 state;
};

// Attention! this struct is always little-endian
CC_PACKED_BEGIN
struct _ESCsm {
	u16 PSA;
	u16 Length;

#if defined(EC_LITTLE_ENDIAN)
	u8 Mode:2;
	u8 Direction:2;
	u8 IntECAT:1;
	u8 IntPDI:1;
	u8 WTE:1;
	u8 R1:1;

	u8 IntW:1;
	u8 IntR:1;
	u8 R2:1;
	u8 MBXstat:1;
	u8 BUFstat:2;
	u8 R3:2;

	u8 ECsm:1;
	u8 ECrep:1;
	u8 ECr4:4;
	u8 EClatchEC:1;
	u8 EClatchPDI:1;

	u8 PDIsm:1;
	u8 PDIrep:1;
	u8 PDIr5:6;
#endif

#if defined(EC_BIG_ENDIAN)
	u8 R1:1;
	u8 WTE:1;
	u8 IntPDI:1;
	u8 IntECAT:1;
	u8 Direction:2;
	u8 Mode:2;

	u8 R3:2;
	u8 BUFstat:2;
	u8 MBXstat:1;
	u8 R2:1;
	u8 IntR:1;
	u8 IntW:1;

	u8 EClatchPDI:1;
	u8 EClatchEC:1;
	u8 ECr4:4;
	u8 ECrep:1;
	u8 ECsm:1;

	u8 PDIr5:6;
	u8 PDIrep:1;
	u8 PDIsm:1;
#endif
} CC_PACKED;
CC_PACKED_END

/* Attention! this struct is always little-endian */
CC_PACKED_BEGIN
struct _ESCsm2 {
	u16 PSA;
	u16 Length;
	u8 Command;
	u8 Status;
	u8 ActESC;
	u8 ActPDI;
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _ESCsmCompact {
	u16 PSA;
	u16 Length;
	u8 Command;
} CC_PACKED;
CC_PACKED_END

struct _ESCvar {
	/* Configuration input is saved so the user variable may go out-of-scope */
	int use_interrupt;
	struct sm_cfg_t  mb[2];
	struct sm_cfg_t  mbboot[2];
	bool skip_default_initialization;
	void (*set_defaults_hook)(void);
	void (*pre_state_change_hook)(u8 *as, u8 *an);
	void (*post_state_change_hook)(u8 *as, u8 *an);
	void (*application_hook)(void);
	void (*safeoutput_override)(void);
	u32 (*pre_object_download_hook)(u16 index,
					u8 subindex,
					void *data,
					size_t size,
					u16 flags);
	u32 (*post_object_download_hook)(u16 index,
					 u8 subindex,
					 u16 flags);
	u32 (*pre_object_upload_hook)(u16 index,
				      u8 subindex,
				      void *data,
				      size_t *size,
				      u16 flags);
	u32 (*post_object_upload_hook)(u16 index,
				       u8 subindex,
				       u16 flags);
	void (*rxpdo_override)(void);
	void (*txpdo_override)(void);
	void (*esc_hw_interrupt_enable)(u32 mask);
	void (*esc_hw_interrupt_disable)(u32 mask);
	void (*esc_hw_eep_handler)(void);
	u16 (*esc_check_dc_handler)(void);
	int (*get_device_id)(u16 *device_id);
	u8 MBXrun;
	u32 activembxsize;
	struct sm_cfg_t *activemb0;
	struct sm_cfg_t *activemb1;
	u16 ESC_SM2_sml;
	u16 ESC_SM3_sml;
	u8 dcsync;
	u16 synccounterlimit;
	u16 ALstatus;
	u16 ALcontrol;
	u16 ALerror;
	u16 DLstatus;
	u16 address;
	u8 mbxcnt;
	u8 mbxincnt;
	u8 mbxoutpost;
	u8 mbxbackup;
	u8 xoe;
	u8 txcue;
	u8 mbxfree;
	u8 segmented;
	void *data;
	u16 entries;
	u32 frags;
	u32 fragsleft;
	u16 index;
	u8 subindex;
	u16 flags;

	u8 toggle;

	int sm2mappings;
	int sm3mappings;

	u8 SMtestresult;
	u32 PrevTime;
	struct _ESCsm SM[4];
	/* Volatile since it may be read from ISR */
	volatile int watchdogcnt;
	volatile u32 Time;
	volatile u32 ALevent;
	volatile s8 synccounter;

	volatile struct _App App;
	u8 mbxdata[PREALLOC_BUFFER_SIZE];
};

CC_PACKED_BEGIN
struct _MBXh {
	u16 length;
	u16 address;

#if defined(EC_LITTLE_ENDIAN)
	u8 channel:6;
	u8 priority:2;

	u8 mbxtype:4;
	u8 mbxcnt:4;
#endif

#if defined(EC_BIG_ENDIAN)
	u8 priority:2;
	u8 channel:6;

	u8 mbxcnt:4;
	u8 mbxtype:4;
#endif
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _MBX {
	struct _MBXh header;
	u8 b[0];
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _COEh {
	u16 numberservice;
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _INFOh {
#if defined(EC_LITTLE_ENDIAN)
	u8 opcode:7;
	u8 incomplete:1;
#endif

#if defined(EC_BIG_ENDIAN)
	u8 incomplete:1;
	u8 opcode:7;
#endif

	u8 reserved;
	u16 fragmentsleft;
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _MBXerr {
	struct _MBXh mbxheader;
	u16 type;
	u16 detail;
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _COEsdo {
	struct _MBXh mbxheader;
	struct _COEh coeheader;
	u8 command;
	u16 index;
	u8 subindex;
	u32 size;
} CC_PACKED CC_ALIGNED(4);
CC_PACKED_END

CC_PACKED_BEGIN
struct _COEobjdesc {
	struct _MBXh mbxheader;
	struct _COEh coeheader;
	struct _INFOh infoheader;
	u16 index;
	u16 datatype;
	u8 maxsub;
	u8 objectcode;
	char name;
} CC_PACKED CC_ALIGNED(4);
CC_PACKED_END

CC_PACKED_BEGIN
struct _COEentdesc {
	struct _MBXh mbxheader;
	struct _COEh coeheader;
	struct _INFOh infoheader;
	u16 index;
	u8 subindex;
	u8 valueinfo;
	u16 datatype;
	u16 bitlength;
	u16 access;
	char name;
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _FOEh {
	u8 opcode;
	u8 reserved;
	union {
		u32 password;
		u32 packetnumber;
		u32 errorcode;
	};
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _FOE {
	struct _MBXh mbxheader;
	struct _FOEh foeheader;
	union {
		char filename[0];
		u8 data[0];
		char errortext[0];
	};
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _EOEh {
	u16 frameinfo1;
	union {
		u16 frameinfo2;
		u16 result;
	};
} CC_PACKED;
CC_PACKED_END

CC_PACKED_BEGIN
struct _EOE {
	struct _MBXh mbxheader;
	struct _EOEh eoeheader;
	u8 data[0];
} CC_PACKED;
CC_PACKED_END

/* state definition in mailbox
 *0 : idle
 *1 : claimed for inbox
 *2 : claimed for outbox
 *3 : request post outbox
 *4 : outbox posted not send
 *5 : backup outbox
 *6 : mailbox needs to be transmitted again
 */
struct _MBXcontrol {
	u8 state;
};

/* Stack reference to application configuration of the ESC */
#define ESC_MBXSIZE         (ESCvar.activembxsize)
#define ESC_MBX0_sma        (ESCvar.activemb0->cfg_sma)
#define ESC_MBX0_sml        (ESCvar.activemb0->cfg_sml)
#define ESC_MBX0_sme        (ESCvar.activemb0->cfg_sme)
#define ESC_MBX0_smc        (ESCvar.activemb0->cfg_smc)
#define ESC_MBX1_sma        (ESCvar.activemb1->cfg_sma)
#define ESC_MBX1_sml        (ESCvar.activemb1->cfg_sml)
#define ESC_MBX1_sme        (ESCvar.activemb1->cfg_sme)
#define ESC_MBX1_smc        (ESCvar.activemb1->cfg_smc)
#define ESC_MBXBUFFERS      (MBXBUFFERS)
#define ESC_SM2_sma         (SM2_sma)
#define ESC_SM2_smc         (SM2_smc)
#define ESC_SM2_act         (SM2_act)
#define ESC_SM3_sma         (SM3_sma)
#define ESC_SM3_smc         (SM3_smc)
#define ESC_SM3_act         (SM3_act)

#define ESC_MBXHSIZE        ((u32)sizeof(struct _MBXh))
#define ESC_MBXDSIZE        (ESC_MBXSIZE - ESC_MBXHSIZE)
#define ESC_FOEHSIZE        ((u32)sizeof(struct _FOEh))
#define ESC_FOE_DATA_SIZE   (ESC_MBXSIZE - (ESC_MBXHSIZE + ESC_FOEHSIZE))
#define ESC_EOEHSIZE        ((u32)sizeof(struct _EOEh))
#define ESC_EOE_DATA_SIZE   (ESC_MBXSIZE - (ESC_MBXHSIZE + ESC_EOEHSIZE))

void ESC_config(struct esc_cfg_t *cfg);
void ESC_ALerror(u16 errornumber);
void ESC_ALeventwrite(u32 event);
u32 ESC_ALeventread(void);
void ESC_ALeventmaskwrite(u32 mask);
u32 ESC_ALeventmaskread(void);
void ESC_ALstatus(u8 status);
void ESC_ALstatusgotoerror(u8 status, u16 errornumber);
void ESC_SMstatus(u8 n);
u8 ESC_WDstatus(void);
u8 ESC_claimbuffer(void);
u8 ESC_startmbx(u8 state);
void ESC_stopmbx(void);
void MBX_error(u16 error);
u8 ESC_mbxprocess(void);
void ESC_xoeprocess(void);
u8 ESC_startinput(u8 state);
void ESC_stopinput(void);
u8 ESC_startoutput(u8 state);
void ESC_stopoutput(void);
void ESC_state(void);
void ESC_sm_act_event(void);

/* From hardware file */
void ESC_read(u16 address, void *buf, u16 len);
void ESC_write(u16 address, void *buf, u16 len);
void ESC_init(const struct esc_cfg_t *cfg);
void ESC_reset(void);

/* From application */
extern void APP_safeoutput(void);
extern struct _ESCvar ESCvar;
extern struct _MBXcontrol MBXcontrol[];
extern u8 MBX[];
extern struct _SMmap SMmap2[];
extern struct _SMmap SMmap3[];

/* ATOMIC operations are used when running interrupt driven */
#ifndef CC_ATOMIC_SET
#define CC_ATOMIC_SET(var, val)   ((var) = (val))
#endif

#ifndef CC_ATOMIC_GET
#define CC_ATOMIC_GET(var)       (var)
#endif

#ifndef CC_ATOMIC_ADD
#define CC_ATOMIC_ADD(var, val)   ((var) += (val))
#endif

#ifndef CC_ATOMIC_SUB
#define CC_ATOMIC_SUB(var, val)   ((var) -= (val))
#endif

#ifndef CC_ATOMIC_AND
#define CC_ATOMIC_AND(var, val)   ((var) &= (val))
#endif

#ifndef CC_ATOMIC_OR
#define CC_ATOMIC_OR(var, val)    ((var) |= (val))
#endif

#endif
