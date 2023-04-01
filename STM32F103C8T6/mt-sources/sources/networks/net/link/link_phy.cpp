#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <semaphore.h>

#include "app.h"
#include "app_if.h"
#include "app_data.h"
#include "app_dbg.h"
#include "task_list.h"

#include "ak.h"
#include "port.h"
#include "message.h"
#include "timer.h"

#include "sys_dbg.h"

#include "link_config.h"
#include "link_sig.h"
#include "link_phy.h"

#define LINK_HAL_HANDLED	1
#define LINK_HAL_IGNORED	0

typedef enum {
	/* private */
	PHY_FRAME_TYPE_ACK = 0x01, /* ack frame */
	PHY_FRAME_TYPE_NACK, /* nack frame */

	/* pulic */
	PHY_FRAME_TYPE_REQ, /* request frame (require target response) */
} phy_frame_type_e;

typedef enum {
	/* private */
	PHY_FRAME_SUB_TYPE_NACK_TO = 0x01,
	PHY_FRAME_SUB_TYPE_NACK_ERR,

	/* pulic */
} phy_frame_sub_type_e;

typedef enum {
	PARSER_STATE_SOF = 0x00,
	PARSER_STATE_DES_ADDR,
	PARSER_STATE_SRC_ADDR,
	PARSER_STATE_TYPE,
	PARSER_STATE_SUB_TYPE,
	PARSER_STATE_SEQ_NUM,
	PARSER_STATE_LEN,
	PARSER_STATE_DATA,
	PARSER_STATE_FCS
} link_phy_frame_parser_state_e;

/* sending data manager */
#define LINK_PHY_SEND_STATE_IDLE		0
#define LINK_PHY_SEND_STATE_SENDING		1
static uint8_t link_phy_send_state = LINK_PHY_SEND_STATE_IDLE;
static void link_phy_send_state_set(uint8_t);
static uint8_t link_phy_send_state_get();

/* retry counter */
static uint8_t retry_counter_send;

/* sequence number */
static uint8_t link_phy_send_seq_num;
static uint8_t link_phy_rev_seq_num;

/* link physic max retry */
static uint8_t link_phy_max_retry_val;

/* static physic frame object */
static link_phy_frame_t send_link_phy_frame;
static link_phy_frame_t rev_link_phy_frame;

/* receive frame parser state */
static pthread_mutex_t mt_link_phy_frame_parser_state_revc;
static pthread_mutex_t mt_link_phy_frame_write;
link_phy_frame_parser_state_e link_phy_frame_parser_state_revc;
link_phy_frame_parser_state_e link_phy_frame_parser_state_revc_get();
void link_phy_frame_parser_state_revc_set(link_phy_frame_parser_state_e);

/* start up state-machine */
fsm_t fsm_link_phy;

/* write data to physical layer declare function */
static void link_phy_frame_write_block(uint8_t* data, uint32_t data_len);
static void link_phy_frame_write(link_phy_frame_t* frame);

/* receive byte calback function */
uint8_t gw_link_phy_frame_rev_byte(uint8_t c);

static uint8_t link_phy_frame_cals_checksum(link_phy_frame_t* phy_frame);

static void link_phy_frame_send_max_retry();
static void link_phy_frame_send();

q_msg_t taskLinkPhyMailbox;

/* serail interface */
#define LINK_PHY_RX_THREAD_BUFFER_SIZE		4096

static int link_phy_fd;
static pthread_t link_phy_rx_thread;
static int link_phy_open_dev(const char* devpath);
static void* link_phy_rx_thread_handler(void*);
static void link_phy_rev_frame_parser(uint8_t* data, uint8_t len);
static void link_phy_rev_frame_start_to();
static void link_phy_rev_frame_clear_to();

static bool comport_is_opened = false;

void* TaskLinkPhyEntry(void*) {
	char *path_tty = filePathRetStr(SERIAL_PORT_INTERFACE);
	if (link_phy_open_dev(path_tty) < 0) {
		APP_PRINT("Cannot open %s !\n", path_tty);
	}
	else {
		APP_PRINT("Opened %s success !\n", path_tty);
		comport_is_opened = true;
		pthread_create(&link_phy_rx_thread, NULL, link_phy_rx_thread_handler, NULL);
	}

	wait_all_tasks_started();

	APP_PRINT("[STARTED] MT_LINK_PHY_ID Entry\n");

	ak_msg_t* msg;

	while (1) {
		/* get messge */
		msg = ak_msg_rev(MT_LINK_PHY_ID);

		fsm_dispatch(&fsm_link_phy, msg);

		/* free message */
		ak_msg_free(msg);
	}
}

void* link_phy_rx_thread_handler(void*) {
	LINK_DBG("link_phy_rx_thread_handler entry successed!\n");
	static uint8_t __rx_thread_handler_buffer[LINK_PHY_RX_THREAD_BUFFER_SIZE];
	static uint32_t __rx_thread_len;

	while(1) {
		__rx_thread_len = read(link_phy_fd, __rx_thread_handler_buffer, LINK_PHY_RX_THREAD_BUFFER_SIZE);
		if (__rx_thread_len > 0) {
			link_phy_rev_frame_parser(__rx_thread_handler_buffer, __rx_thread_len);
		}
	}

	return (void*)0;
}

int link_phy_open_dev(const char* devpath) {
	struct termios options;
	LINK_DBG("[LINK_PHY][link_phy_open_dev] devpath: %s\n", devpath);

	link_phy_fd = open(devpath, O_RDWR | O_NOCTTY | O_NDELAY);
	if (link_phy_fd < 0) {
		return link_phy_fd;
	}
	else {
		fcntl(link_phy_fd, F_SETFL, 0);

		/* get current status */
		tcgetattr(link_phy_fd, &options);

		cfsetispeed(&options, B115200);
		cfsetospeed(&options, B115200);

		/* No parity (8N1) */
		options.c_cflag &= ~PARENB;
		options.c_cflag &= ~CSTOPB;
		options.c_cflag &= ~CSIZE;
		options.c_cflag |= CS8;

		options.c_cflag |= (CLOCAL | CREAD);
		options.c_cflag     &=  ~CRTSCTS;

		cfmakeraw(&options);

		tcflush(link_phy_fd, TCIFLUSH);
		if (tcsetattr (link_phy_fd, TCSANOW, &options) != 0) {
			SYS_DBG("error in tcsetattr()\n");
		}
	}
	return 0;
}

void link_phy_rev_frame_parser(uint8_t* data, uint8_t len) {
	for (uint32_t i = 0; i < len; i++) {
		if (gw_link_phy_frame_rev_byte(*(data + i)) == LINK_HAL_IGNORED) {
			RAW_DBG("%c", *(data + i));
			if (link_phy_frame_parser_state_revc_get() != PARSER_STATE_SOF) {
				link_phy_frame_parser_state_revc_set(PARSER_STATE_SOF);
				timer_remove_attr(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV_TO);
			}
		}
	}
}

void task_link_phy(ak_msg_t* msg) {
	fsm_dispatch(&fsm_link_phy, msg);
}

void link_phy_send_state_set(uint8_t state) {
	link_phy_send_state = state;
	LINK_DBG("[PHY] link_phy_send_state -> %d\n", state);
}

uint8_t link_phy_send_state_get() {
	return link_phy_send_state;
}

void link_phy_frame_write_block(uint8_t* data, uint32_t data_len) {
	int ret =  write(link_phy_fd, (uint8_t*)data, data_len);

	if (ret < 0) {
		FATAL("LK_PHY", 0x05);
	}
}

uint32_t link_phy_get_send_frame_to() {
	uint32_t ret = ((link_phy_max_retry_val + 1) * LINK_PHY_FRAME_SEND_TO_INTERVAL);
	return ret;
}

void link_phy_frame_write(link_phy_frame_t* frame) {
	if (comport_is_opened) {
        pthread_mutex_lock(&mt_link_phy_frame_write);
		/* write frame header */
		link_phy_frame_write_block((uint8_t*)frame, sizeof(link_phy_frame_header_t));

		/* write frame data */
		if (frame->header.len > 0) {
			link_phy_frame_write_block((uint8_t*)frame->data, frame->header.len);
		}
		pthread_mutex_unlock(&mt_link_phy_frame_write);
	}
}

uint8_t link_phy_frame_cals_checksum(link_phy_frame_t* phy_frame) {
	uint8_t* frame_header = (uint8_t*)phy_frame;
	uint8_t ret_check_sum = 0;

	/* cals checksum of header frame */
	for (uint32_t i = 0; i < (sizeof(link_phy_frame_header_t) - sizeof(uint8_t)); i++) {
		ret_check_sum ^= *(frame_header + i);
	}

	/* cals checksum of data frame */
	for (uint32_t i = 0; i < phy_frame->header.len; i++) {
		ret_check_sum ^= phy_frame->data[i];
	}

	return (uint8_t)ret_check_sum;
}

void fsm_link_phy_state_init(ak_msg_t* msg) {
	switch (msg->header->sig) {
	case GW_LINK_PHY_INIT: {
		LINK_DBG_SIG("GW_LINK_PHY_INIT\n");

		/* private object init */
		link_phy_send_state_set(LINK_PHY_SEND_STATE_IDLE);
		retry_counter_send = 0;

		link_phy_send_seq_num = (uint8_t)rand();
		link_phy_rev_seq_num = (uint8_t)rand();

		send_link_phy_frame.header.sof = LINK_PHY_SOF;
		rev_link_phy_frame.header.sof = LINK_PHY_SOF;

		link_phy_frame_parser_state_revc = PARSER_STATE_SOF;

		link_phy_max_retry_set(LINK_PHY_MAX_RETRY_SET_DEFAULT);

		FSM_TRAN(&fsm_link_phy, fsm_link_phy_state_handle);

		task_post_pure_msg(MT_LINK_MAC_ID, GW_LINK_MAC_PHY_LAYER_STARTED);
	}
		break;

	default:
		break;
	}
}

void fsm_link_phy_state_handle(ak_msg_t* msg) {
	switch (msg->header->sig) {
	case GW_LINK_PHY_FRAME_SEND_REQ: {
		LINK_DBG_SIG("GW_LINK_PHY_FRAME_SEND_REQ\n");
		if (link_phy_send_state_get() == LINK_PHY_SEND_STATE_IDLE) {
			/* sending frame packed */
			send_link_phy_frame.header.type = (uint8_t)PHY_FRAME_TYPE_REQ;
			send_link_phy_frame.header.sub_type = 0;
			send_link_phy_frame.header.seq_num = ++link_phy_send_seq_num;
			send_link_phy_frame.header.len = get_data_len_common_msg(msg);
			if (send_link_phy_frame.header.len > LINK_PHY_FRAME_SIZE) {
				FATAL("LK_PHY", 0x01);
			}

			get_data_common_msg(msg, send_link_phy_frame.data, send_link_phy_frame.header.len);
			send_link_phy_frame.header.fcs = link_phy_frame_cals_checksum((link_phy_frame_t*)&send_link_phy_frame);

			retry_counter_send = 0;
			link_phy_send_state_set(LINK_PHY_SEND_STATE_SENDING);

			link_phy_frame_send();
		}
		else {
			FATAL("LK_PHY", 0x02);
		}
	}
		break;

	case GW_LINK_PHY_FRAME_SEND_TO: {
		LINK_DBG_SIG("GW_LINK_PHY_FRAME_SEND_TO\n");
		if (retry_counter_send >= link_phy_max_retry_val) {
			link_phy_frame_send_max_retry();
		}
		else {
			link_phy_frame_send();
		}
		retry_counter_send++;
	}
		break;

	case GW_LINK_PHY_FRAME_REV: {
		LINK_DBG_SIG("GW_LINK_PHY_FRAME_REV\n");
		link_phy_frame_t* link_frame_rev = (link_phy_frame_t*)get_data_common_msg(msg);

		switch (link_frame_rev->header.type) {
		case PHY_FRAME_TYPE_REQ: {
			if (link_phy_rev_seq_num != link_frame_rev->header.seq_num) {
				link_phy_rev_seq_num = link_frame_rev->header.seq_num;
			}

			/* respond ack */
			link_phy_frame_t link_phy_frame_ack;
			link_phy_frame_ack.header.sof = LINK_PHY_SOF;
			link_phy_frame_ack.header.des_addr = link_frame_rev->header.src_addr;
			link_phy_frame_ack.header.src_addr = link_frame_rev->header.des_addr;
			link_phy_frame_ack.header.type = PHY_FRAME_TYPE_ACK;
			link_phy_frame_ack.header.sub_type = 0;
			link_phy_frame_ack.header.seq_num = link_frame_rev->header.seq_num;
			link_phy_frame_ack.header.len = 0;
			link_phy_frame_ack.header.fcs = link_phy_frame_cals_checksum((link_phy_frame_t*)&link_phy_frame_ack);
			link_phy_frame_write(&link_phy_frame_ack);

			task_post_common_msg(MT_LINK_MAC_ID, GW_LINK_MAC_FRAME_REV, (uint8_t*)link_frame_rev->data, link_frame_rev->header.len);
		}
			break;

		case PHY_FRAME_TYPE_ACK: {
			if (link_phy_send_state_get() == LINK_PHY_SEND_STATE_SENDING) {
				if (link_phy_send_seq_num == link_frame_rev->header.seq_num) {
					timer_remove_attr(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_SEND_TO);
					link_phy_send_state_set(LINK_PHY_SEND_STATE_IDLE);
					task_post_pure_msg(MT_LINK_MAC_ID, GW_LINK_MAC_FRAME_SEND_DONE);
				}
			}
		}
			break;

		case PHY_FRAME_TYPE_NACK: {
			if (link_phy_send_state_get() == LINK_PHY_SEND_STATE_SENDING) {
				if (link_phy_send_seq_num == link_frame_rev->header.seq_num) {
					timer_remove_attr(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_SEND_TO);

					if (retry_counter_send >= link_phy_max_retry_val) {
						link_phy_frame_send_max_retry();
					}
					else {
						link_phy_frame_send();
					}
					retry_counter_send++;
				}
			}
		}
			break;

		default:
			break;
		}
	}
		break;

	case GW_LINK_PHY_FRAME_REV_CS_ERR: {
		LINK_DBG_SIG("GW_LINK_PHY_FRAME_REV_CS_ERR\n");
		link_phy_frame_t* link_frame_rev = (link_phy_frame_t*)get_data_common_msg(msg);

		/* respond non-ack */
		link_phy_frame_t link_phy_frame_non_ack;
		link_phy_frame_non_ack.header.sof = LINK_PHY_SOF;
		link_phy_frame_non_ack.header.des_addr = link_frame_rev->header.src_addr;
		link_phy_frame_non_ack.header.src_addr = link_frame_rev->header.des_addr;
		link_phy_frame_non_ack.header.type = PHY_FRAME_TYPE_NACK;
		link_phy_frame_non_ack.header.sub_type = 0;
		link_phy_frame_non_ack.header.seq_num = link_frame_rev->header.seq_num;
		link_phy_frame_non_ack.header.len = 0;
		link_phy_frame_non_ack.header.fcs = link_phy_frame_cals_checksum((link_phy_frame_t*)&link_phy_frame_non_ack);
		link_phy_frame_write(&link_phy_frame_non_ack);
	}
		break;

	case GW_LINK_PHY_FRAME_REV_TO: {
		LINK_DBG_SIG("GW_LINK_PHY_FRAME_REV_TO\n");
		link_phy_frame_parser_state_revc_set(PARSER_STATE_SOF);
	}
		break;

	default:
		break;
	}
}

void link_phy_frame_send_max_retry() {
	link_phy_send_state_set(LINK_PHY_SEND_STATE_IDLE);
	task_post_pure_msg(MT_LINK_MAC_ID, GW_LINK_MAC_FRAME_SEND_ERR);
}

void link_phy_frame_send() {
	if (link_phy_send_state_get() == LINK_PHY_SEND_STATE_SENDING) {
		link_phy_frame_write(&send_link_phy_frame);
		timer_set(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_SEND_TO, LINK_PHY_FRAME_SEND_TO_INTERVAL, TIMER_ONE_SHOT);
	}
}

void link_phy_max_retry_set(uint8_t max_retry) {
	link_phy_max_retry_val = max_retry;
}

uint8_t link_phy_max_retry_get() {
	return link_phy_max_retry_val;
}

uint8_t gw_link_phy_frame_rev_byte(uint8_t c) {
	static uint8_t link_phy_util_index;
	uint8_t ret_handle = LINK_HAL_HANDLED;

	switch (link_phy_frame_parser_state_revc_get()) {
	case PARSER_STATE_SOF: {
		if (LINK_PHY_SOF == c) {
			link_phy_util_index = 3;
			link_phy_frame_parser_state_revc_set(PARSER_STATE_DES_ADDR);
			link_phy_rev_frame_start_to();
		}
		else {
			ret_handle = LINK_HAL_IGNORED;
		}
	}
		break;

	case PARSER_STATE_DES_ADDR: {
		((uint8_t*)&rev_link_phy_frame.header.des_addr)[link_phy_util_index] = c;
		if (link_phy_util_index == 0) {
			link_phy_util_index = 3;
			link_phy_frame_parser_state_revc_set(PARSER_STATE_SRC_ADDR);
		}
		else {
			link_phy_util_index--;
		}
	}
		break;

	case PARSER_STATE_SRC_ADDR: {
		((uint8_t*)&rev_link_phy_frame.header.src_addr)[link_phy_util_index] = c;
		if (link_phy_util_index == 0) {
			link_phy_frame_parser_state_revc_set(PARSER_STATE_TYPE);
		}
		else {
			link_phy_util_index--;
		}
	}
		break;

	case PARSER_STATE_TYPE: {
		rev_link_phy_frame.header.type = c;
		link_phy_frame_parser_state_revc_set(PARSER_STATE_SUB_TYPE);
	}
		break;

	case PARSER_STATE_SUB_TYPE: {
		rev_link_phy_frame.header.sub_type = c;
		link_phy_frame_parser_state_revc_set(PARSER_STATE_SEQ_NUM);
	}
		break;

	case PARSER_STATE_SEQ_NUM: {
		rev_link_phy_frame.header.seq_num = c;
		link_phy_frame_parser_state_revc_set(PARSER_STATE_LEN);
	}
		break;

	case PARSER_STATE_LEN: {
		rev_link_phy_frame.header.len = c;
		if (rev_link_phy_frame.header.len > LINK_PHY_FRAME_SIZE) {
			link_phy_frame_parser_state_revc_set(PARSER_STATE_SOF);
			timer_remove_attr(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV_TO);
			//FATAL("LK_PHY", 0x04); // Only for internal link layer 
			
			break;
		}
		link_phy_frame_parser_state_revc_set(PARSER_STATE_FCS);
	}
		break;

    case PARSER_STATE_FCS: {
		rev_link_phy_frame.header.fcs = c;

		if (rev_link_phy_frame.header.len > 0) {
			link_phy_util_index = 0;
			link_phy_frame_parser_state_revc_set(PARSER_STATE_DATA);
		}
		else {
			link_phy_rev_frame_clear_to();

			uint8_t cals_cs = link_phy_frame_cals_checksum(&rev_link_phy_frame);

			if (cals_cs == rev_link_phy_frame.header.fcs) {
				task_post_common_msg(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV, (uint8_t*)&rev_link_phy_frame, sizeof(link_phy_frame_t));
			}
			else {
				LINK_DBG("checksum incorrectly !\n");
				task_post_common_msg(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV_CS_ERR, (uint8_t*)&rev_link_phy_frame, sizeof(link_phy_frame_t));
			}

			link_phy_frame_parser_state_revc_set(PARSER_STATE_SOF);
		}
	}
		break;

    case PARSER_STATE_DATA: {
		rev_link_phy_frame.data[link_phy_util_index++] = c;

		if (link_phy_util_index == rev_link_phy_frame.header.len) {
			link_phy_rev_frame_clear_to();

			uint8_t cals_cs = link_phy_frame_cals_checksum(&rev_link_phy_frame);

			if (cals_cs == rev_link_phy_frame.header.fcs) {
				task_post_common_msg(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV, (uint8_t*)&rev_link_phy_frame, sizeof(link_phy_frame_t));
			}
			else {
				LINK_DBG("checksum incorrectly !\n");
				task_post_common_msg(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV_CS_ERR, (uint8_t*)&rev_link_phy_frame, sizeof(link_phy_frame_t));
			}

			link_phy_frame_parser_state_revc_set(PARSER_STATE_SOF);
		}
	}
		break;

	default:
		ret_handle = LINK_HAL_IGNORED;
		break;
	}

	return ret_handle;
}

void link_phy_rev_frame_start_to() {
#if 1
	timer_set(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV_TO, LINK_PHY_FRAME_REV_TO_INTERVAL, TIMER_ONE_SHOT);
#endif
}

void link_phy_rev_frame_clear_to() {
#if 1
	timer_remove_attr(MT_LINK_PHY_ID, GW_LINK_PHY_FRAME_REV_TO);
#endif
}

link_phy_frame_parser_state_e link_phy_frame_parser_state_revc_get() {
	link_phy_frame_parser_state_e ret_state;
	pthread_mutex_lock(&mt_link_phy_frame_parser_state_revc);
	ret_state = link_phy_frame_parser_state_revc;
	pthread_mutex_unlock(&mt_link_phy_frame_parser_state_revc);
	return ret_state;
}

void link_phy_frame_parser_state_revc_set(link_phy_frame_parser_state_e state) {
	pthread_mutex_lock(&mt_link_phy_frame_parser_state_revc);
	link_phy_frame_parser_state_revc = state;
	pthread_mutex_unlock(&mt_link_phy_frame_parser_state_revc);
}
