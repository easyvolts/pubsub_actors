/*
============================================================================
Name        : pubsub.h
Author      : Valerii Proskurin
Version     : v 0.0.1 alpha
Copyright   : Copyright (c) 2023, Valerii Proskurin. All rights reserved.
Description : actors model implementation for simple and safe 
multitasking in C using only static memory allocation and intended for use
in embedded systems.
License     : SPDX-License-Identifier: GPL-3.0-or-later OR commercial.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

In addition, this program is available under a commercial license
from author (Valerii Proskurin). If you do not wish to be bound by the
terms of the GPL, or you require a more permissive license for commercial use,
please contact author via easyvolts@gmail.com for licensing options.
============================================================================
============================================================================
*/

#include <stdio.h>
#include <stdint.h>

#define PS_MAX_TOPICS_COUNT					(3)
#define PS_MAX_ACTORS_COUNT					(3)
#define PS_MAX_TOPIC_PATH_STR_LENGTH		(64)
#define PS_MAX_TOPIC_INFO_STR_LENGTH		(64)
#define PS_MAX_SUBSCRIBER_INFO_STR_LENGTH	(64)
#define PS_MAX_MESSAGE_PAYLOAD_LENGTH		(64)
#define PS_MSG_HDR_LENGTH					(sizeof(PsTopicHash_t) + sizeof(psMsgLen_t)) //to get msg size in the queue add payload size to the header size.
//names of the pub/sub dispatcher serviced topics
#define PS_SYS_SERVICED_PERIODIC_MS_TIMER_TOPIC ".srv.t_ms.tick" //periodic timers
#define PS_SYS_SERVICED_SINGLE_MS_TIMER_TOPIC   ".srv.t_ms.tout"   //single shot timers
#define PS_SYS_SERVICED_TOPICS_CHANGE_TOPIC     ".srv.tpc.chng"   //changes in the topics list (adding and removing topics will be indicated here).

//typeof data encapsulated in the IPC message
typedef enum {
	PS_DTYPE_NONE = 0,
	PS_DTYPE_U8,
	PS_DTYPE_I8,
	PS_DTYPE_U16,
	PS_DTYPE_I16,
	PS_DTYPE_U32,
	PS_DTYPE_I32,
	PS_DTYPE_U64,
	PS_DTYPE_I64,
	PS_DTYPE_TIMESTAMP,
	PS_DTYPE_BYTEARRAY,
	PS_DTYPE_STR,
	PS_DTYPE_BOOL,
	PS_DTYPE_COUNT
} PsDataType_e;

typedef enum {
	PS_RESULT_REDEF_CONFLICT = -5,
	PS_RESULT_OUT_OF_MEM = -4,
	PS_RESULT_DUPLICATED = -3,
	PS_RESULT_NOT_FOUND = -2,
	PS_RESULT_ERROR = -1,
	PS_RESULT_OK = 0,
	PS_RESULT_APPENDED,
	PS_RESULT_CREATED,	
} PsResultType_e;

typedef uint16_t PsMsgLen_t;
typedef uint16_t PsTopicHash_t;
//pointer to function that will handle message (actor)
typedef const char * (*actor_f)(PsTopicHash_t xTopicHash, void* pvMsg, size_t xMsgLendth, PsDataType_e xMsgDataType);
typedef void(*restart_timer_f)(long int tout_ms);
typedef long int(*get_timer_tick_ms_f)();


//*******************************   Basic API ***************************************************
/** Attention!!!
    All APIs here are not thread safe, so to use it as part of different threads or inside an IRQ - you have to wrap it in critical sections.
*/

//returns -1 if failed, 0 - if ok.
PsResultType_e ps_init(restart_timer_f pxRestart_timer, get_timer_tick_ms_f pxGet_timer_tick_ms);

PsResultType_e ps_register_topic_publisher(actor_f pxActorHandler, PsDataType_e xDataType, const char * pu8TopicPathStr, const char * pu8TopicInfoStr, uint8_t u8Sticky_flag, PsTopicHash_t * pxTopicHash);
PsResultType_e ps_unregister_topic_publisher(actor_f pxActorHandler, PsTopicHash_t xTopicHash);


PsResultType_e ps_pub_topic_with_registration(actor_f pxActorHandler, PsDataType_e xDataType, const char * pu8TopicPathStr, const char * pu8TopicInfoStr, uint8_t u8Sticky_flag, PsMsgLen_t xMsgLen, void * pvData, PsTopicHash_t * pxTopicHash);
/** @brief posts a message into a queue
*  @param  xTopicHash - hash of topic to which we are going to post.
*  @param  xMsgLen - length of the message to post.
*  @param  pvData - pointer to the data of the message to post.
*  @return  result of the operation as PsResultType_e type.
*  @note if ps_pub_topic will be called for the topic from another publisher - it will disturb ongoing frame content.
*  To prevent this always wrap complete frame publishing routine into critical section. For exmple:
*	enter_critical_section();
*  ps_pub_topic(your_topic.frame.start);
*	ps_pub_topic(your_topic.field_of_frame1);
*	....
*	ps_pub_topic(your_topic.field_of_framex);
*  ps_pub_topic(your_topic.frame.end);
*	exit_critical_section();
*/
PsResultType_e ps_pub_topic(actor_f pxActorHandler, PsTopicHash_t xTopicHash, PsMsgLen_t xMsgLen, void * pvData);
PsResultType_e ps_sub_single_topic(const char * pu8TopicPathStr, PsDataType_e xDataType, actor_f pxActorHandler, PsTopicHash_t * pxTopicHash, void** pvMsg, size_t * pxMsgLendth, PsDataType_e * pxMsgDataType);
PsResultType_e ps_unsub_topic(const char * pu8TopicPathStr, actor_f pxActorHandler);
PsResultType_e ps_create_and_sub_timer_topic(const char * pu8TopicPathStr, actor_f pxActorHandler, const char * pu8TopicInfoStr, long int tout_ms);

PsResultType_e ps_check_topic(const char * pu8TopicPathStr, PsDataType_e * pxDataType, char * pu8TopicInfoStr, PsTopicHash_t * pxTopicHash);
PsResultType_e ps_check_topic_by_hash(PsTopicHash_t xTopicHash, const char ** pu8TopicPathStr, const char ** pu8TopicInfoStr, PsDataType_e * pxDataType);

const char * ps_check_subscriber(actor_f pxSubscriber);

void ps_pub_timer_tout_event();

//returns -1 if failed, otherwise - count of events waiting to be processed.
int16_t ps_get_waiting_events_count();

uint8_t ps_has_enough_msg_space(size_t bytes_to_publish);

//returns -1 if failed, otherwise - count of processed events.
int16_t ps_loop();

//*******************************   Extended optional API ***************************************************

//mute exact event source (identified by publisher + topic). It leaves possibility to publish into the topic for other sources. 
//This functionality is intended to be used for testing/debugging by sustituting some event sources with test events triggered via console.
PsResultType_e ps_pub_mute(actor_f pxActorHandler, const char * pu8TopicPathStr, uint8_t u8MuteFlag);
PsResultType_e ps_pub_mute_by_hash(actor_f pxActorHandler, PsTopicHash_t xTopicHash, uint8_t u8MuteFlag);
PsResultType_e ps_create_and_sub_tpc_change_topic(actor_f pxActorHandler);