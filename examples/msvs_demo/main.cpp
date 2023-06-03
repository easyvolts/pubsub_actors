/*
============================================================================
Name        : main.c
Author      : Valerii Proskurin
Version     :
Copyright   : Copyright (c) 2023, Valerii Proskurin. All rights reserved.
Description : Example project for demonstration of pubsub_actor library.
============================================================================
*/

/* plantuml diagram for suffix tree that describes the demo system.
@startmindmap
top to bottom direction
* . (root topic)
** .hw (hw specific topics)
*** .tty
**** .data(array uint8)
**** .cfg
***** .speed(uint32)
***** .bits(uint8)
***** .parity(char)
***** .stop(uint8)
** .srv (common service topics)
*** .nvm (key-value storage)
**** .your_key\n(publishing to it\n will save new value)
*** .tpc (topics changes)
**** .reg (bool, de-/registration)
*** .tmr (timers)
**** .ms (millisec)
***** .tick (periodic)
****** .your_tick_name
******* .period(set)
******* .evt(get)
***** .tout (single)
****** .your_tout_name
******* .duration(set)
******* .evt(get)
@endmindmap
*/

#define WIN32_LEAN_AND_MEAN      // Exclude rarely-used stuff from Windows headers

#include <windows.h>

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "../../pubsub_actors/pubsub.h"

#define EMBEDDED_CLI_IMPL
#include "../../3rd_party/embedded_cli/embedded_cli.h"

// 164 bytes is minimum size for this params on Arduino Nano
#define CLI_BUFFER_SIZE 	512
#define CLI_RX_BUFFER_SIZE 	16
#define CLI_CMD_BUFFER_SIZE 128
#define CLI_HISTORY_SIZE 	128
#define CLI_BINDING_COUNT 	3

EmbeddedCli *cli;

CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
char stdin_buf[100];

static bool exitFlag = false;

const char * reader_act(uint16_t u16TopicHash, void* pvMsg, size_t xMsgLendth, PsDataType_e xMsgDataType) {
	const char * pTopicPathStr, * pTopicInfoStr;
	PsDataType_e dtype;

	if (NULL != pvMsg) {
		switch (xMsgDataType)
		{
		case PS_DTYPE_BOOL:			
			ps_check_topic_by_hash(u16TopicHash, &pTopicPathStr, &pTopicInfoStr, &dtype);
			printf("Received bool %u from %s topic (%s) \r\n", *(uint8_t*)pvMsg, pTopicPathStr, pTopicInfoStr);
			break;
		case PS_DTYPE_STR:
			printf("SYSTEM - %.*s\r\n", xMsgLendth, (const char *)pvMsg);
			break;
		default:
			printf("Unsupported Dtype %u\r\n", xMsgDataType);
			break;
		}
	}	
	return "reader/consumer actor\r\n";
}

void reader_init() {
	ps_create_and_sub_tpc_change_topic(reader_act);
	ps_sub_single_topic("sys.console.bool", PS_DTYPE_BOOL, reader_act, NULL, NULL, NULL, NULL);
}


const char * console_act(uint16_t u16TopicHash, void* pvMsg, size_t xMsgLendth, PsDataType_e xMsgDataType) {
	if (NULL != pvMsg) {
		const char * pcTopicPath;
		ps_check_topic_by_hash(u16TopicHash, &pcTopicPath, NULL, NULL);
		printf("Received %s msg\r\n", pcTopicPath);
	}
	return "console actor\r\n";
}

void writeChar(EmbeddedCli *embeddedCli, char c) {
	putchar(c);
}

void onCommand(EmbeddedCli *embeddedCli, CliCommand *command) {
	printf("Received command:\r\n");
	printf("%s\r\n", command->name);
	embeddedCliTokenizeArgs(command->args);
	for (int i = 1; i <= embeddedCliGetTokenCount(command->args); ++i) {
		printf("arg ");
		printf("%c", ('0' + i));
		printf(": ");
		printf("%s\r\n", embeddedCliGetToken(command->args, i));
	}
}

void onHello(EmbeddedCli *cli, char *args, void *context) {
	printf("Hello ");
	if (embeddedCliGetTokenCount(args) == 0)
		printf((const char *)context);
	else
		printf(embeddedCliGetToken(args, 1));
	printf("\r\n");
}

void onPub(EmbeddedCli *cli, char *args, void *context) {
	printf("publish ");
	if (embeddedCliGetTokenCount(args) == 0) {
		printf("bool true to sys.console.bool");
		uint8_t bool_var = 1;
		ps_pub_topic(console_act, *(uint16_t *)context, 1, (void *)&bool_var);
	}
	else if (embeddedCliGetTokenCount(args) == 1) {
		printf("bool true to %s", embeddedCliGetToken(args, 1));
		uint16_t u16TopicHash;
		PsResultType_e result = ps_register_topic_publisher(console_act, PS_DTYPE_BOOL, embeddedCliGetToken(args, 1), "boolean messages from console", false, &u16TopicHash);
		if (PS_RESULT_OK != result) {
			printf("ERROR: result=%i\r\n", result);
		}
		uint8_t bool_var = 1;
		ps_pub_topic(console_act, u16TopicHash, 1, (void *)&bool_var);
	}
	else {
		uint8_t bool_var;
		if (embeddedCliGetToken(args, 1)[0] == 't') {
			bool_var = 1;
			printf("bool %s to %s", embeddedCliGetToken(args, 1), embeddedCliGetToken(args, 2));
			ps_pub_topic_with_registration(console_act, PS_DTYPE_BOOL, embeddedCliGetToken(args, 2), "console arbitrary bool topic pub\r\n", 0, 1, (void *)&bool_var, NULL);
		}
		else if(embeddedCliGetToken(args, 1)[0] == 'f'){
			bool_var = 0;
			printf("bool %s to %s", embeddedCliGetToken(args, 1), embeddedCliGetToken(args, 2));
			ps_pub_topic_with_registration(console_act, PS_DTYPE_BOOL, embeddedCliGetToken(args, 2), "console arbitrary bool topic pub\r\n", 0, 1, (void *)&bool_var, NULL);
		}
		else {
			//number
			long int li_var = atol(embeddedCliGetToken(args, 1));
			printf("create and sub timer %s as %s", embeddedCliGetToken(args, 1), embeddedCliGetToken(args, 2));
			ps_create_and_sub_timer_topic(embeddedCliGetToken(args, 2), console_act,"console timer topic create\r\n", li_var);
		}
		
	}
	printf("\r\n");
}

void onMute(EmbeddedCli *cli, char *args, void *context) {
	printf("mute ");
	if (embeddedCliGetTokenCount(args) == 0) {
		printf(" sys.console.bool");
		ps_pub_mute(console_act, "sys.console.bool", 1);
	}
	else if (embeddedCliGetTokenCount(args) == 1) {
		printf(" %s", embeddedCliGetToken(args, 1));
		ps_pub_mute(console_act, embeddedCliGetToken(args, 1), 1);
	}
	else if (embeddedCliGetTokenCount(args) == 2) {
		printf(" %s set %s", embeddedCliGetToken(args, 1), embeddedCliGetToken(args, 2));
		uint8_t mute_flag = embeddedCliGetToken(args, 2)[0] - '0';
		ps_pub_mute(console_act, embeddedCliGetToken(args, 1), mute_flag);
	}
	else {
		printf("wrong arg, only one parameter (topic to mute) is allowed");
	}
	printf("\r\n");
}

void onQ(EmbeddedCli *cli, char *args, void *context) {
	printf("Exit..\r\n");
	exitFlag = true;
}

uint16_t u16ConsolePublishTopicHash;

void console_init() {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, stdin_buf, _IONBF, sizeof(stdin_buf));
	//setvbuf(stdin, NULL, _IONBF, 0);

	EmbeddedCliConfig *config = embeddedCliDefaultConfig();
	config->cliBuffer = cliBuffer;
	config->cliBufferSize = CLI_BUFFER_SIZE;
	config->rxBufferSize = CLI_RX_BUFFER_SIZE;
	config->cmdBufferSize = CLI_CMD_BUFFER_SIZE;
	config->historyBufferSize = CLI_HISTORY_SIZE;
	config->maxBindingCount = CLI_BINDING_COUNT;
	config->enableAutoComplete = false;
	cli = embeddedCliNew(config);

	//register pub topics
	PsResultType_e result = ps_register_topic_publisher(console_act, PS_DTYPE_BOOL, "sys.console.bool", "boolean messages from console", false, &u16ConsolePublishTopicHash);
	if (PS_RESULT_OK != result) {
		printf("ERROR: result=%i\r\n", result);
	}

	if (cli == NULL) {
		printf("Cli was not created. Check sizes!\r\n");
		return;
	}
	printf("Cli has started. Enter your commands or type help for details.\r\n");

	embeddedCliAddBinding(cli, CliCommandBinding {
		"pub",
			"Publish a msg, for example \" pub 3000 .srv.t_ms.tick.3s\" or simply \"pub\"",
			true,
			(void *)&u16ConsolePublishTopicHash,
			onPub
	});

	embeddedCliAddBinding(cli, CliCommandBinding{
		"mute",
		"Mute a topic",
		true,
		(void *)&u16ConsolePublishTopicHash,
		onMute
	});

	embeddedCliAddBinding(cli, CliCommandBinding {
		"q",
			"Stop CLI and quit",
			false,
			NULL,
			onQ
	});

	cli->onCommand = onCommand;
	cli->writeChar = writeChar;
}

int getch_noblock() {
	if (_kbhit())
		return _getch();
	else
		return -1;
}

void console() {
	int c = getch_noblock();
	if (-1 != c) {
		embeddedCliReceiveChar(cli, (char)c);
		embeddedCliProcess(cli);
	}
}

long int timer_tick_ms = 0;
long int timer_tout_ms = 0;

void restart_timer(long int tout_ms) {
	timer_tick_ms = 0;
	timer_tout_ms = tout_ms;
}

long int get_timer_tick_ms() {
	return timer_tick_ms;
}

//imitate timer interrupts
void loop_timer(void) {
	Sleep(100); //100ms sleep
	if (timer_tout_ms) {
		//we have some timer to run
		timer_tick_ms += 100;
		if (timer_tick_ms >= timer_tout_ms) {
			//timer expired - notify pubsub dispatcher
			ps_pub_timer_tout_event();
		}
	}
}

void check_sleep(int busy) {
	static int old_busy = 1;
	if (old_busy > 0) {
		if (busy == 0) {
			printf("---\\___ Start sleep\r\n");
			old_busy = busy;
		}
	} else {
		if (busy != 0) {
			printf("___/--- End sleep\r\n");
			old_busy = busy;
		}
	}
}

int main(void) {
	ps_init(restart_timer, get_timer_tick_ms);
	reader_init();
	console_init();	

	while (!exitFlag) {
		if (cli == NULL) break;		
		loop_timer();
		check_sleep(ps_get_waiting_events_count());
		ps_loop();
		console(); //read new events
	}
	return EXIT_SUCCESS;
}

