  /*
  ============================================================================
  Name        : cirqular_queue.h
  Author      : Valerii Proskurin
  Version     : v 0.0.1
  Copyright   : Copyright (c) 2023, Valerii Proskurin. All rights reserved.
  Description : This file contains API functions prototypes for circular FIFO queue with
  elements of variable size located in a statically allocated array. Not thread safe!
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef CQ_H
#define CQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Struct that defines element's header.
 */
typedef struct _CQ_ELEM_HEADER_S
{
	size_t size; //element size in bytes
} CQ_ELEM_HEADER_S;

/**
 * Struct that defines Queue entity.
 */
typedef struct _CQ_S
{
	void * pDataBegin; //pointer to static array which will be used to store all data.
	void * pDataEnd;
	size_t totalSize;   //total buffer size in bytes
	void * pRear; //pointer to the rear element
	void * pFront; //pointer to the front element
	size_t freeSize; //number of free bytes in the buffer
	int count; //number of elements in the queue
} CQ_S;

/**
 * @brief Initializes FIFO queue with parameters of its buffer array and element size.
 * @param pQueue pointer to CQ_S variable to be initialized.
 * @param qBuffer byte array to be used as a storage for queue data.
 * @param qBufferSize size of qBuffer in bytes.
 */
void cq_init(CQ_S *pQueue, void * qBuffer, size_t qBufferSize);

/**
 * @brief removes head element from the CQ.
 * @param pQueue pointer to the queue.
 * @return length of the removed data in bytes if success, otherwise - 0.
 */
size_t cq_deleteFrontElement(CQ_S *pQueue);

/**
 * @brief copies head element from the CQ into destination buffer.
 * @param pQueue pointer to queue.
 * @param pDest pointer to buffer where element data will be copied from the queue buffer.
 * @param destMaxSize max number of bytes that can be written in pDest addr.
 * @return length of the data in bytes if success, otherwise - 0.
 */
size_t cq_getFrontElement(CQ_S *pQueue, void * pDest, size_t destMaxSize);

/**
 * @brief adds tail element to the CQ by copying an element to queue's internal buffer.
 * @param pQueue pointer to the queue
 * @param pNewElement pointer to an element to be added in the end of the queue.
 * @param elementSize length of the element to be added in the queue (bytes).
 * @return true if success, otherwise - false (for example if queue is full and adding new element is not possible).
 */
bool cq_addTailElement(CQ_S *pQueue, void * pNewElement, size_t elementSize);

/**
 * @brief returns number of the elements in the queue.
 * @param pQueue pointer to queue.
 * @return number of the elements in the queue.
 */
int cq_count(CQ_S *pQueue);

/**
 * @brief checks if the queue has enough space to hold the element of specific size.
 * @param pQueue pointer to queue.
 * @param elementSize size of the element to be checked if it fits in the queue.
 * @return true if there is enough space, false - otherwise.
 */
bool cq_hasSpace(CQ_S *pQueue, size_t elementSize);

/**
 * @brief clears content of the queue.
 * @param pQueue pointer to the queue
 */
void cq_flush(CQ_S *pQueue);

#ifdef __cplusplus
}
#endif

#endif //CQ_H
