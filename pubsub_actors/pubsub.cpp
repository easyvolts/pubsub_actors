/*
============================================================================
Name        : pubsub.c
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
*/

#include "pubsub.h"
#include "circular_queue.h"
#include <string.h>

typedef uint16_t PsActorId_t;

//header part of IPC messages (actor mail header).
typedef struct _PsMsgStructHdr_s {
	PsTopicHash_t xTopicHash;
	PsMsgLen_t xMsgLen;
} PsMsgStructHdr_s;

typedef struct _PsMsgStruct_s {
	PsMsgStructHdr_s xHdr;
	uint8_t  pu8Data[PS_MAX_MESSAGE_PAYLOAD_LENGTH];
} PsMsgStruct_s;

typedef struct _PsTopicStruct_s {
	PsTopicHash_t u16Hash;
	PsDataType_e xDtype;
	uint8_t u8Sticky_flag;
	char pu8TopicPathStr[PS_MAX_TOPIC_PATH_STR_LENGTH];
	char pu8TopicInfoStr[PS_MAX_TOPIC_INFO_STR_LENGTH];
	actor_f pxSubscribers[PS_MAX_ACTORS_COUNT];
	actor_f pxPublishers[PS_MAX_ACTORS_COUNT];
	uint8_t u8PublishersMute[PS_MAX_ACTORS_COUNT];
	PsMsgStruct_s xLastMsg;
} PsTopicStruct_s;

typedef struct _PsTimerStruct_s {
	PsTopicHash_t u16Hash;
	actor_f xCreatorPublisher; //only one topic creating publisher is allowed (but for debug we can inject timer events from other publishers).
	long int duration_ms;
	long int time_left_ms;
	uint8_t periodic_flag;
} PsTimerStruct_s;

//*********** private function prototypes
PsResultType_e ps_pub_topic(PsTopicHash_t xTopicHash, PsMsgLen_t xMsgLen, void * pvData);

static PsTopicStruct_s TopicsArray[PS_MAX_TOPICS_COUNT] = { 0, };
static PsTimerStruct_s TimersArray[PS_MAX_TOPICS_COUNT] = { 0, };
static CQ_S msg_queue = { 0 };
static uint8_t msg_queue_buf[1024] = { 0, };
static restart_timer_f restart_timer;
static get_timer_tick_ms_f get_timer_tick_ms;
static PsTopicHash_t xTopic_tpc_cnhg;
static uint8_t u8Topic_tpc_cnhg_present_flag = 0;

//returns -1 if failed, 0 - if ok.
PsResultType_e ps_init(restart_timer_f pxRestart_timer, get_timer_tick_ms_f pxGet_timer_tick_ms) {
	cq_init(&msg_queue, (char*)msg_queue_buf, sizeof(msg_queue_buf));
	memset(TopicsArray,0,sizeof(TopicsArray));
	restart_timer = pxRestart_timer;
	get_timer_tick_ms = pxGet_timer_tick_ms;
	return PS_RESULT_OK; //TODO
}

void ps_report_topic_change(PsTopicHash_t xTopicHash, const char * prefix) {
	if (u8Topic_tpc_cnhg_present_flag) {
		//notify about topic remove
		char msg_str[PS_MAX_MESSAGE_PAYLOAD_LENGTH];
		int length = snprintf(msg_str, sizeof(msg_str), "%s %u %s[%u]", prefix, xTopicHash, TopicsArray[xTopicHash].pu8TopicPathStr, TopicsArray[xTopicHash].xDtype);
		ps_pub_topic(NULL, xTopic_tpc_cnhg, length, msg_str);
	}
}

PsResultType_e ps_find_topic(const char * pu8TopicPathStr, PsTopicHash_t * pxTopicHash) {
	for (PsTopicHash_t i = 0; i < PS_MAX_TOPICS_COUNT; i++) {
		if (NULL != pu8TopicPathStr) {
			if ('\0' == TopicsArray[i].pu8TopicPathStr[0]) continue;
			if (0 == strncmp(TopicsArray[i].pu8TopicPathStr, pu8TopicPathStr, sizeof(TopicsArray[i].pu8TopicPathStr))) {
				//found existing topic
				*pxTopicHash = i;
				return PS_RESULT_OK;
			}
		} else {
			if ('\0' == TopicsArray[i].pu8TopicPathStr[0]) {
				//found empty topic slot
				*pxTopicHash = i;
				return PS_RESULT_OK;
			}
		}

	}
	//neither topic nor space for it found
	return PS_RESULT_NOT_FOUND;
}

uint8_t ps_is_all_zero(void * mem, size_t length) {
	uint8_t allZero = 1;
	uint8_t * array = (uint8_t *)mem;
	for (size_t i = 0; i < length; i++) {
		if(array[i]) {
			return 0;
		}
	}
	return 1;
}

PsResultType_e ps_find_actor(actor_f pxActors[PS_MAX_ACTORS_COUNT], actor_f pxActorHandler, PsActorId_t * pxActorIdx) {
	for (PsActorId_t i = 0; i < PS_MAX_ACTORS_COUNT; i++) {
		if (pxActorHandler == pxActors[i]) {
			//found existing actor
			*pxActorIdx = i;
			return PS_RESULT_OK;
		}
	}
	//actor not found
	return PS_RESULT_NOT_FOUND;
}

PsResultType_e ps_register_actor(actor_f pxActors[PS_MAX_ACTORS_COUNT], actor_f pxActorHandler, PsActorId_t * pxActorIdx) {
	PsActorId_t xActorIdx = 0;
	if (PS_RESULT_OK == ps_find_actor(pxActors, pxActorHandler, &xActorIdx)) {
		if(NULL != pxActorIdx) *pxActorIdx = xActorIdx;
		return PS_RESULT_DUPLICATED;
	} 
	//actor not found, find and use an empty slot in actor array
	if (PS_RESULT_OK == ps_find_actor(pxActors, NULL, &xActorIdx)) {
		if (NULL != pxActorIdx) *pxActorIdx = xActorIdx;
		pxActors[xActorIdx] = pxActorHandler;
		return PS_RESULT_OK;
	}
	//no empty slot and no actor found in the array, it's unexpected case and thereby fatal error!
	return PS_RESULT_ERROR;
}

PsResultType_e ps_register_topic_publisher(actor_f pxActorHandler, PsDataType_e xDataType, const char * pu8TopicPathStr, const char * pu8TopicInfoStr, uint8_t u8Sticky_flag, PsTopicHash_t * pxTopicHash) {
	PsTopicHash_t xTopicHash = 0;
	if (PS_RESULT_OK == ps_find_topic(pu8TopicPathStr, &xTopicHash)) {
		*pxTopicHash = xTopicHash;
		//we already have this topic registered, just add publisher
		if (0 == ps_is_all_zero(TopicsArray[xTopicHash].pxPublishers, sizeof(TopicsArray[xTopicHash].pxPublishers))) {
			TopicsArray[xTopicHash].u8Sticky_flag |= u8Sticky_flag;
			if (xDataType != TopicsArray[xTopicHash].xDtype) {
				return PS_RESULT_REDEF_CONFLICT;
			}
		}
		if (PS_RESULT_ERROR != ps_register_actor(TopicsArray[xTopicHash].pxPublishers, pxActorHandler, NULL)) {
			return PS_RESULT_OK;
		}
	} else {
		//topic not found and has to be created
		if (PS_RESULT_OK == ps_find_topic(NULL, &xTopicHash)) {
			*pxTopicHash = xTopicHash;
			TopicsArray[xTopicHash].u8Sticky_flag = u8Sticky_flag;
			TopicsArray[xTopicHash].xDtype = xDataType;
			//we found empty slot for the topic, just add publisher
			if (PS_RESULT_ERROR != ps_register_actor(TopicsArray[xTopicHash].pxPublishers, pxActorHandler, NULL)) {
				strncpy(TopicsArray[xTopicHash].pu8TopicInfoStr, pu8TopicInfoStr, sizeof(TopicsArray[xTopicHash].pu8TopicInfoStr));
				strncpy(TopicsArray[xTopicHash].pu8TopicPathStr, pu8TopicPathStr, sizeof(TopicsArray[xTopicHash].pu8TopicPathStr));		
				ps_report_topic_change(xTopicHash, "ADD");
				return PS_RESULT_OK;
			} 			
		}
	}
	return PS_RESULT_ERROR;
}

PsResultType_e ps_pub_topic_with_registration(actor_f pxActorHandler, PsDataType_e xDataType, const char * pu8TopicPathStr, const char * pu8TopicInfoStr, uint8_t u8Sticky_flag, PsMsgLen_t xMsgLen, void * pvData, PsTopicHash_t * pxTopicHash) {
	PsTopicHash_t topic_hash;
	//register new topic if necessary
	PsResultType_e result = ps_register_topic_publisher(pxActorHandler, xDataType, pu8TopicPathStr, pu8TopicInfoStr, u8Sticky_flag, &topic_hash);
	if (PS_RESULT_OK != result) return result;
	if(NULL != pxTopicHash) *pxTopicHash = topic_hash;
	//publish the message
	return ps_pub_topic(pxActorHandler, topic_hash, xMsgLen, pvData);
}

PsResultType_e ps_manage_topic(PsTopicHash_t xTopicHash) {
	if (NULL == TopicsArray[xTopicHash].pu8TopicPathStr) return PS_RESULT_NOT_FOUND;
	//check if we still have publishers for the topic
	for (PsActorId_t i = 0; i < PS_MAX_ACTORS_COUNT; i++) {
		if (NULL != TopicsArray[xTopicHash].pxPublishers[i]) {
			//found active publisher, don't remove topic
			return PS_RESULT_OK;
		}
	}
	//no active publishers found, check if we still have subscribers for the topic
	for (PsActorId_t i = 0; i < PS_MAX_ACTORS_COUNT; i++) {
		if (NULL != TopicsArray[xTopicHash].pxPublishers[i]) {
			//found active subscriber, don't remove topic
			return PS_RESULT_OK;
		}
	}
	//no active publishers or subscribers - remove topic to free slot in the topic array
	ps_report_topic_change(xTopicHash, "DEL");
	if (xTopic_tpc_cnhg == xTopicHash) {
		u8Topic_tpc_cnhg_present_flag = 0;
	}
	memset(&TopicsArray[xTopicHash], 0, sizeof(TopicsArray[xTopicHash]));
	return PS_RESULT_OK;
}

PsResultType_e ps_unregister_topic_publisher(actor_f pxActorHandler, PsTopicHash_t xTopicHash) {
	PsActorId_t xActorIdx = 0;
	//we already have this topic registered, just add subscriber
	if (NULL == TopicsArray[xTopicHash].pu8TopicPathStr) return PS_RESULT_NOT_FOUND;
	if (PS_RESULT_ERROR != ps_find_actor(TopicsArray[xTopicHash].pxPublishers, pxActorHandler, &xActorIdx)) {
		TopicsArray[xTopicHash].pxPublishers[xActorIdx] = NULL;
		TopicsArray[xTopicHash].u8PublishersMute[xActorIdx] = 0;
		return ps_manage_topic(xTopicHash);
	}
	return PS_RESULT_ERROR;
}

PsResultType_e ps_pub_topic(actor_f pxActorHandler, PsTopicHash_t xTopicHash, PsMsgLen_t xMsgLen, void * pvData){
	if (NULL == TopicsArray[xTopicHash].pu8TopicPathStr) return PS_RESULT_NOT_FOUND;
	TopicsArray[xTopicHash].xLastMsg.xHdr.xTopicHash = xTopicHash;
	TopicsArray[xTopicHash].xLastMsg.xHdr.xMsgLen = xMsgLen;
	if(NULL != pvData) memcpy(TopicsArray[xTopicHash].xLastMsg.pu8Data, pvData, sizeof(TopicsArray[xTopicHash].xLastMsg.pu8Data));
	//publish
	PsActorId_t xActorIdx = 0;
	PsResultType_e result = ps_find_actor(TopicsArray[xTopicHash].pxPublishers, pxActorHandler, &xActorIdx);
	if(PS_RESULT_OK != result) {
		return result;
	}
	if (0 == TopicsArray[xTopicHash].u8PublishersMute[xActorIdx]) {
		if (0 == cq_addTailElement(&msg_queue, (void*)&TopicsArray[xTopicHash].xLastMsg, sizeof(TopicsArray[xTopicHash].xLastMsg.xHdr) + xMsgLen)) {
			return PS_RESULT_OUT_OF_MEM;
		}
	}
	return PS_RESULT_OK;
}
PsResultType_e ps_sub_single_topic(const char * pu8TopicPathStr, PsDataType_e xDataType, actor_f pxActorHandler, PsTopicHash_t * pxTopicHash, void** pvMsg, size_t * pxMsgLendth, PsDataType_e * pxMsgDataType) {
	PsTopicHash_t xTopicHash;
	//check if we already have the topic
	PsResultType_e result = ps_find_topic(pu8TopicPathStr, &xTopicHash);
	//if not - create it before subscribing
	if (PS_RESULT_NOT_FOUND == result) {
		//create topic without publisher
		PsResultType_e result = ps_find_topic(NULL, &xTopicHash);
		if (PS_RESULT_OK != result) return result;
		// just add subscriber to an incomplete topic (we don't have data type,"sticky" flag and info str)
		strncpy(TopicsArray[xTopicHash].pu8TopicPathStr, pu8TopicPathStr, sizeof(TopicsArray[xTopicHash].pu8TopicPathStr));
		TopicsArray[xTopicHash].xDtype = xDataType;
		ps_report_topic_change(xTopicHash, "ADD");
	}	
	//we have the topic - subscribe
	if(NULL != pxTopicHash) *pxTopicHash = xTopicHash;
	if (PS_RESULT_ERROR != ps_register_actor(TopicsArray[xTopicHash].pxSubscribers, pxActorHandler, NULL)) {
		if ((NULL != pvMsg)&&(NULL != pxMsgLendth)&&(NULL != pxMsgDataType)&&(TopicsArray[xTopicHash].u8Sticky_flag)) {
			//we have "sticky" topic, so inform subscriber about data currently available for the topic
			*pvMsg = TopicsArray[xTopicHash].xLastMsg.pu8Data;
			*pxMsgLendth = TopicsArray[xTopicHash].xLastMsg.xHdr.xMsgLen;
			*pxMsgDataType = TopicsArray[xTopicHash].xDtype;
		}
		return PS_RESULT_OK;
	}
	return PS_RESULT_ERROR;
}

PsResultType_e ps_unsub_topic(const char * pu8TopicPathStr, actor_f pxActorHandler) {
	PsActorId_t xActorIdx = 0;
	PsTopicHash_t xTopicHash;
	PsResultType_e result = ps_find_topic(pu8TopicPathStr, &xTopicHash);
	if (PS_RESULT_OK != result) return result;
	//we found the topic, remove subscriber
	if (NULL == TopicsArray[xTopicHash].pu8TopicPathStr) return PS_RESULT_NOT_FOUND;
	if (PS_RESULT_ERROR != ps_find_actor(TopicsArray[xTopicHash].pxSubscribers, pxActorHandler, &xActorIdx)) {
		TopicsArray[xTopicHash].pxSubscribers[xActorIdx] = NULL;
		return ps_manage_topic(xTopicHash);
	}
	return PS_RESULT_ERROR;
}

PsResultType_e ps_check_topic(const char * pu8TopicPathStr, PsDataType_e * pxDataType, char * pu8TopicInfoStr, PsTopicHash_t * pxTopicHash) {
	PsTopicHash_t xTopicHash;
	PsResultType_e result = ps_find_topic(pu8TopicPathStr, &xTopicHash);
	if (PS_RESULT_OK != result) return result;
	//we found the topic
	*pxTopicHash = xTopicHash;
	strncpy(pu8TopicInfoStr, TopicsArray[xTopicHash].pu8TopicInfoStr, sizeof(TopicsArray[xTopicHash].pu8TopicInfoStr));
	*pxDataType = TopicsArray[xTopicHash].xDtype;
	return PS_RESULT_OK;
}

PsResultType_e ps_check_topic_by_hash(PsTopicHash_t xTopicHash, const char ** ppu8TopicPathStr, const char ** ppu8TopicInfoStr, PsDataType_e * pxDataType) {
	if (NULL == TopicsArray[xTopicHash].pu8TopicPathStr) return PS_RESULT_NOT_FOUND;
	//we found the topic
	if (NULL != ppu8TopicPathStr) *ppu8TopicPathStr = (const char *)TopicsArray[xTopicHash].pu8TopicPathStr;
	if (NULL != ppu8TopicInfoStr) *ppu8TopicInfoStr = (const char *)TopicsArray[xTopicHash].pu8TopicInfoStr;
	if (NULL != pxDataType) *pxDataType = TopicsArray[xTopicHash].xDtype;
	return PS_RESULT_OK;
}

const char * ps_check_subscriber(actor_f pxSubscriber) {
	return pxSubscriber(0, NULL, 0, PS_DTYPE_NONE);
}

//returns -1 if failed, otherwise - count of messages in the queue.
int16_t ps_loop() {
	PsMsgStruct_s msg;
	unsigned int processed_messages_count = 0;
	//if we are the only consumer for the queue, we can consider extracting elements as thread safe.
	if (cq_getFrontElement(&msg_queue, &msg, sizeof(msg))) {
		for (PsActorId_t u16Actor_idx = 0;u16Actor_idx < PS_MAX_ACTORS_COUNT;u16Actor_idx++) {
			actor_f actor = TopicsArray[msg.xHdr.xTopicHash].pxSubscribers[u16Actor_idx];
			if (NULL != actor) {
				(void)actor(msg.xHdr.xTopicHash, msg.pu8Data, msg.xHdr.xMsgLen, TopicsArray[msg.xHdr.xTopicHash].xDtype);
			}
		}	
		cq_deleteFrontElement(&msg_queue);
		processed_messages_count++;
	}
	return processed_messages_count;
}

int16_t ps_get_waiting_events_count() {
	return cq_count(&msg_queue);
}

uint8_t ps_str_starts_with(const char *prefix, const char *str)
{
	size_t lenprefix = strlen(prefix);
	size_t lenstr = strlen(str);
	if (lenstr < lenprefix) {
		return 0;
	}
	else if (memcmp(prefix, str, lenprefix)) {
		return 0;
	}
	return 1;
}

//timer topic must have at least one subscriber, otherwise it will be remowed automatically.
PsResultType_e ps_create_and_sub_timer_topic(const char * pu8TopicPathStr, actor_f pxActorHandler, const char * pu8TopicInfoStr, long int tout_ms) {
	//we want to create a new timer topic.
	//a. check if this is a new timer topic, if not - return fail;
	//b. find empty slot and add timer in the list;
	//c. call ps_timer_tout_event() to check timers list in case we now have to resturt running timer;
	PsTopicHash_t xTopicHash;
	//do a).
	PsResultType_e result = ps_find_topic(pu8TopicPathStr, &xTopicHash);
	if (PS_RESULT_OK == result) return PS_RESULT_DUPLICATED;
	int8_t periodic_flag = -1;
	periodic_flag += ps_str_starts_with(PS_SYS_SERVICED_PERIODIC_MS_TIMER_TOPIC, pu8TopicPathStr) << 1;
	periodic_flag += ps_str_starts_with(PS_SYS_SERVICED_SINGLE_MS_TIMER_TOPIC, pu8TopicPathStr);
	if((periodic_flag < 0)||(periodic_flag > 1)) {
		//we support only timer topic paths that start with either "tmr.ms.periodic" or "tmr.ms.single" 
		return PS_RESULT_NOT_FOUND;
	}
	result = ps_register_topic_publisher(pxActorHandler, PS_DTYPE_NONE, pu8TopicPathStr, pu8TopicInfoStr, false, &xTopicHash);
	if (PS_RESULT_OK != result) return result;
	void* pvMsg;
	size_t xMsgLendth;
	PsDataType_e xMsgDataType;
	result = ps_sub_single_topic(pu8TopicPathStr, PS_DTYPE_NONE, pxActorHandler, NULL, &pvMsg, &xMsgLendth, &xMsgDataType);
	if (PS_RESULT_OK != result) return result;
	//do b).
	for (PsActorId_t i = 0; i < PS_MAX_ACTORS_COUNT; i++) {
		if (0 == TimersArray[i].duration_ms) {
			//found free slot
			TimersArray[i].u16Hash = xTopicHash;
			TimersArray[i].xCreatorPublisher = pxActorHandler;
			TimersArray[i].duration_ms = tout_ms;
			TimersArray[i].time_left_ms = tout_ms;
			TimersArray[i].periodic_flag = periodic_flag;
			//do c).
			ps_pub_timer_tout_event();
			return PS_RESULT_OK;
		}
	}
	return PS_RESULT_OUT_OF_MEM;	
}

//topic.change [.tpc.cnhg] topic must have at least one subscriber, otherwise it will be remowed automatically.
PsResultType_e ps_create_and_sub_tpc_change_topic(actor_f pxActorHandler) {
	const char * pu8TopicPathStr = PS_SYS_SERVICED_TOPICS_CHANGE_TOPIC;
	const char * pu8TopicInfoStr = "serviced topic, prints string info about adding/removing topics in the system, format: \"ADD/DEL HASH topic_name_str\"";
	//we want to create a new serviced topic to see all added/removed topics in the system for debugging purposes.
	//a. check if this is a new tpc.chng topic, if yes - create publisher;
	//b. subscribe to the topic;
	PsTopicHash_t xTopicHash;
	//do a).
	PsResultType_e result = ps_find_topic(pu8TopicPathStr, &xTopicHash);
	if (PS_RESULT_NOT_FOUND == result) {
		result = ps_register_topic_publisher(NULL, PS_DTYPE_STR, pu8TopicPathStr, pu8TopicInfoStr, false, &xTopicHash);
		if (PS_RESULT_OK != result) return result;
		xTopic_tpc_cnhg = xTopicHash;
		u8Topic_tpc_cnhg_present_flag = 1;
	}
	//do b).
	result = ps_sub_single_topic(pu8TopicPathStr, PS_DTYPE_STR, pxActorHandler, NULL, NULL, NULL, NULL);
	if (PS_RESULT_OK != result) return result;
	return PS_RESULT_OK;
}

void ps_pub_timer_tout_event() {
	//our timer has expired, process timers table.
	//a) reduce all ticks_left variables in timers table by duration of the expired timer;
	//b) send notification to all topic which timers have expired;
	//c) start smallest timeout in the table;
	long int current_tick_ms = get_timer_tick_ms();
	long int shortest_timer_ms = INT32_MAX;
	for (PsActorId_t i = 0; i < PS_MAX_ACTORS_COUNT; i++) {
		if (0 == TimersArray[i].duration_ms) continue; //emtpy timer slot, skip it
		//do a).
		TimersArray[i].time_left_ms -= current_tick_ms;
		//do b)
		if (TimersArray[i].time_left_ms <= 0) {
			//this timer expired, publish tout event
			ps_pub_topic(TimersArray[i].xCreatorPublisher, TimersArray[i].u16Hash, PS_DTYPE_NONE, NULL);
			//rewind timer if this is a periodic event
			if (TimersArray[i].periodic_flag) {
				TimersArray[i].time_left_ms = TimersArray[i].duration_ms;
			}
		}
		//find shortest interval to start timer again
		if (TimersArray[i].time_left_ms > 0) {
			if (TimersArray[i].time_left_ms < shortest_timer_ms) {
				shortest_timer_ms = TimersArray[i].time_left_ms;
			}
		}
	}
	//do c)
	if (shortest_timer_ms != 0) {
		//re-/start hw timer only if we have some timer topics running.
		restart_timer(shortest_timer_ms);
	}
}


uint8_t ps_has_enough_msg_space(size_t bytes_to_publish) {
	return (uint8_t)cq_hasSpace(&msg_queue, bytes_to_publish);
}


PsResultType_e ps_pub_mute(actor_f pxActorHandler, const char * pu8TopicPathStr, uint8_t u8MuteFlag) {
	PsTopicHash_t xTopicHash;
	PsActorId_t xActorIdx = 0;
	PsResultType_e result = ps_find_topic(pu8TopicPathStr, &xTopicHash);
	if (PS_RESULT_OK != result) return result;
	return ps_pub_mute_by_hash(pxActorHandler, xTopicHash, u8MuteFlag);
}

PsResultType_e ps_pub_mute_by_hash(actor_f pxActorHandler, PsTopicHash_t xTopicHash, uint8_t u8MuteFlag) {
	PsActorId_t xActorIdx = 0;
	PsResultType_e result = ps_find_actor(TopicsArray[xTopicHash].pxPublishers, pxActorHandler, &xActorIdx);
	if (PS_RESULT_OK != result) {
		return result;
	}
	TopicsArray[xTopicHash].u8PublishersMute[xActorIdx] = u8MuteFlag;
	return PS_RESULT_OK;
}