// rtcom from the DS side: the probe's own version, written from what
// TWPatch's TwlBg does (its frame hook reads the clock's registers 0x112 and
// 0x113 and answers by raising the ARM7's RTC interrupt).
//
// The DS reaches those two registers through the clock's serial port, with
// commands sent high bit first: 0x6C/0x6D write/read the data byte, 0x6E/0x6F
// the status byte. Status, from the DS: below 0x80 a new command, 0x81 the
// next byte, 0x83 "say no", 0xFE close. From the 3DS: 0x80 yes, 0x82 no or
// done, 0x00 closed.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RTCOM_ACK 0x80
#define RTCOM_NAK 0x82
#define RTCOM_NEXT 0x81
#define RTCOM_SAY_NO 0x83
#define RTCOM_CLOSE 0xFE
#define RTCOM_UPLOAD 0x41
#define RTCOM_MAKE_RUNNABLE 0x42
#define RTCOM_RUN 0x44

// The clock's bit delay: long while uploading, short after.
void rtcomSetFast(bool fast);

// RCNT set so that the 3DS's answers raise the interrupt; and put back.
void rtcomBegin(void);
void rtcomEnd(void);

// The status the 3DS answers with, or -1 if it did not within about 100 ms.
int rtcomWait(void);

void rtcomWriteData(uint8_t value);
uint8_t rtcomReadData(void);
void rtcomWriteStatus(uint8_t value);

// The alarm's three bytes (the payload's extra channel).
void rtcomWriteExtra(const uint8_t bytes[3]);
void rtcomReadExtra(uint8_t bytes[3]);
// The first alarm's three bytes (command 0x69), a plain clock read.
void rtcomReadAlarm1(uint8_t bytes[3]);

// A request and its answer: true if the 3DS said yes (0x80).
bool rtcomAsk(uint8_t status, uint8_t data);

bool rtcomUpload(const uint8_t *code, uint32_t size, int32_t *failAt);
// Runs the uploaded program with a first byte; *reply gets its answer.
bool rtcomRun(uint8_t first, uint8_t *reply);
bool rtcomNext(uint8_t value, uint8_t *reply);
// Ends a session as the known clients do: close, wait, then 0x82.
void rtcomClose(void);
