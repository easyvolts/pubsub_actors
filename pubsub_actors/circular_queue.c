/*
============================================================================
Name        : cirqular_queue.c
Author      : Valerii Proskurin
Version     : v 0.0.1
Copyright   : Copyright (c) 2023, Valerii Proskurin. All rights reserved.
Description : implementation of circular FIFO queue with elements of variable
size located in a statically allocated array. Not thread safe!
Must be called ONLY from safe section to avoid queue corruption.
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
#include "circular_queue.h"

#define PRINT_DEBUG_CQ(...) // PRINT_INFO("CQ",__VA_ARGS__)

void cq_flush(CQ_S *pQueue) {
	pQueue->pRear = pQueue->pDataBegin;
	pQueue->pFront = pQueue->pDataBegin;
	pQueue->count = 0;
	pQueue->freeSize = pQueue->totalSize;
}

void cq_init(CQ_S *pQueue, void * qBuffer, size_t qBufferSize)
{
	pQueue->pDataBegin = qBuffer;
	pQueue->pDataEnd = (char*)qBuffer + qBufferSize;
	pQueue->totalSize = qBufferSize;
	cq_flush(pQueue);
}

bool cq_hasSpace(CQ_S *pQueue, size_t elementSize) {
	return (pQueue->freeSize >= (elementSize + sizeof(CQ_ELEM_HEADER_S)));
}

void * cq_wrappedCopyToBuff(CQ_S *pQueue, void * pStart, void * pSource, size_t length) {
	size_t i = 0;
	for (i = 0; i < length; ++i) {
		*(char*)pStart = *(char*)pSource;
		pStart = (char*)pStart + 1;
		pSource = (char*)pSource + 1;
		if (pStart >= pQueue->pDataEnd) pStart = pQueue->pDataBegin;
	}
	return pStart;
}

//returns pointer to the next message in the circular buffer
void * cq_wrappedCopyFromBuff(CQ_S *pQueue, void * pStart, void * pDest, size_t length) {
	size_t i = 0;
	for (i = 0; i < length; ++i) {
		*(char*)pDest = *(char*)pStart;
		pStart = (char*)pStart + 1;
		pDest = (char*)pDest + 1;
		if (pStart >= pQueue->pDataEnd) pStart = pQueue->pDataBegin;
	}
	return pStart;
}

bool cq_addTailElement(CQ_S *pQueue, void * pNewElement, size_t elementSize)
{
	if (0 == elementSize) return false; //empty records are not allowed
	if (!cq_hasSpace(pQueue, elementSize))
	{
		PRINT_DEBUG_CQ("Queue Overflow\r\n");
		return false;
	}
	else {
		PRINT_DEBUG_CQ("Insert element %u ", pQueue->count + 1);
		//build header
		CQ_ELEM_HEADER_S header = { 0, };
		header.size = elementSize;
		//copy header and data into the data buffer of the queue and update queue info
		void * pEnd = cq_wrappedCopyToBuff(pQueue, pQueue->pRear, (void*)&header, sizeof(header));
		pQueue->pRear = cq_wrappedCopyToBuff(pQueue, (char*)pEnd, (char*)pNewElement, elementSize);
		pQueue->count++;
		pQueue->freeSize -= (elementSize + sizeof(header));
		return true;
	}
}

size_t cq_getFrontElement(CQ_S *pQueue, void * pDest, size_t destMaxSize)
{
	CQ_ELEM_HEADER_S header;
	void * pData;
	size_t bytesToRead = 0;
	if (pQueue->count > 0)
	{
		pData = cq_wrappedCopyFromBuff(pQueue, pQueue->pFront, (void*)&header, sizeof(header));
		bytesToRead = header.size;
		if (destMaxSize < header.size) bytesToRead = destMaxSize;
		(void)cq_wrappedCopyFromBuff(pQueue, (void*)pData, (void*)pDest, bytesToRead);
		return bytesToRead;
	}
	else {
		//queue is empty
		return 0;
	}
}

size_t cq_deleteFrontElement(CQ_S *pQueue)
{
	CQ_ELEM_HEADER_S header;
	if (pQueue->count > 0)
	{
		(void)cq_wrappedCopyFromBuff(pQueue, pQueue->pFront, (char*)&header, sizeof(header));
		void * pvTemp = (char*)pQueue->pFront + header.size + sizeof(header);
		if (pvTemp >= pQueue->pDataEnd) {
			pvTemp = (char*)pvTemp - pQueue->totalSize;
		}
		pQueue->pFront = pvTemp;
		pQueue->count--;
		pQueue->freeSize += (header.size + sizeof(header));
		return header.size;
	}
	else {
		//queue is empty
		return 0;
	}
}

int cq_count(CQ_S *pQueue) {
	return pQueue->count;
}
