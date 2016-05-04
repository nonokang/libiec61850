/*
 *  logging.c
 *
 *  Copyright 2016 Michael Zillgith
 *
 *  This file is part of libIEC61850.
 *
 *  libIEC61850 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  libIEC61850 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with libIEC61850.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  See COPYING file for the complete license text.
 */

#include "libiec61850_platform_includes.h"
#include "stack_config.h"
#include "mms_mapping.h"
#include "logging.h"
#include "linked_list.h"
#include "array_list.h"
#include "hal_thread.h"

#include "simple_allocator.h"
#include "mem_alloc_linked_list.h"

#include "mms_mapping_internal.h"
#include "mms_value_internal.h"


LogControl*
LogControl_create(LogicalNode* parentLN)
{
    LogControl* self = (LogControl*) GLOBAL_MALLOC(sizeof(LogControl));

    self->enabled = false;
    self->dataSet = NULL;
    self->triggerOps = 0;

    return self;
}

void
LogControl_destroy(LogControl* self)
{
    if (self != NULL) {
        GLOBAL_FREEMEM(self);
    }
}

static LogControlBlock*
getLCBForLogicalNodeWithIndex(MmsMapping* self, LogicalNode* logicalNode, int index)
{
    int lcbCount = 0;

    LogControlBlock* nextLcb = self->model->lcbs;

    while (nextLcb != NULL ) {
        if (nextLcb->parent == logicalNode) {

            if (lcbCount == index)
                return nextLcb;

            lcbCount++;

        }

        nextLcb = nextLcb->sibling;
    }

    return NULL ;
}

static char*
createDataSetReferenceForDefaultDataSet(LogControlBlock* lcb, LogControl* logControl)
{
    char* dataSetReference;

    char* domainName = MmsDomain_getName(logControl->domain);
    char* lnName = lcb->parent->name;

    dataSetReference = createString(5, domainName, "/", lnName, "$", lcb->dataSetName);

    return dataSetReference;
}

static MmsValue*
createTrgOps(LogControlBlock* reportControlBlock) {
    MmsValue* trgOps = MmsValue_newBitString(-6);

    uint8_t triggerOps = reportControlBlock->trgOps;

    if (triggerOps & TRG_OPT_DATA_CHANGED)
        MmsValue_setBitStringBit(trgOps, 1, true);
    if (triggerOps & TRG_OPT_QUALITY_CHANGED)
        MmsValue_setBitStringBit(trgOps, 2, true);
    if (triggerOps & TRG_OPT_DATA_UPDATE)
        MmsValue_setBitStringBit(trgOps, 3, true);
    if (triggerOps & TRG_OPT_INTEGRITY)
        MmsValue_setBitStringBit(trgOps, 4, true);

    //TODO remove - GI doesn't exist here!
    if (triggerOps & TRG_OPT_GI)
        MmsValue_setBitStringBit(trgOps, 5, true);

    return trgOps;
}

static MmsVariableSpecification*
createLogControlBlock(LogControlBlock* logControlBlock,
        LogControl* logControl)
{
    MmsVariableSpecification* rcb = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    rcb->name = copyString(logControlBlock->name);
    rcb->type = MMS_STRUCTURE;

    MmsValue* mmsValue = (MmsValue*) GLOBAL_CALLOC(1, sizeof(MmsValue));
    mmsValue->deleteValue = false;
    mmsValue->type = MMS_STRUCTURE;

    int structSize = 9;

    mmsValue->value.structure.size = structSize;
    mmsValue->value.structure.components = (MmsValue**) GLOBAL_CALLOC(structSize, sizeof(MmsValue*));

    rcb->typeSpec.structure.elementCount = structSize;

    rcb->typeSpec.structure.elements = (MmsVariableSpecification**) GLOBAL_CALLOC(structSize,
            sizeof(MmsVariableSpecification*));

    /* LogEna */
    MmsVariableSpecification* namedVariable =
            (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));

    namedVariable->name = copyString("LogEna");
    namedVariable->type = MMS_BOOLEAN;

    rcb->typeSpec.structure.elements[0] = namedVariable;
    mmsValue->value.structure.components[0] = MmsValue_newBoolean(logControlBlock->logEna);

    /* LogRef */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("LogRef");
    namedVariable->typeSpec.visibleString = -129;
    namedVariable->type = MMS_VISIBLE_STRING;
    rcb->typeSpec.structure.elements[1] = namedVariable;

    if (logControlBlock->logRef != NULL) {
        mmsValue->value.structure.components[1] = MmsValue_newVisibleString(logControlBlock->logRef);
    }
    else {
        char* logRef = createString(4, logControl->domain->domainName, "/", logControlBlock->parent->name,
                "$GeneralLog");

        mmsValue->value.structure.components[1] = MmsValue_newVisibleString(logRef);

        GLOBAL_FREEMEM(logRef);
    }

    /* DatSet */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("DatSet");
    namedVariable->typeSpec.visibleString = -129;
    namedVariable->type = MMS_VISIBLE_STRING;
    rcb->typeSpec.structure.elements[2] = namedVariable;

    if (logControlBlock->dataSetName != NULL) {
        char* dataSetReference = createDataSetReferenceForDefaultDataSet(logControlBlock, logControl);

        mmsValue->value.structure.components[2] = MmsValue_newVisibleString(dataSetReference);
        GLOBAL_FREEMEM(dataSetReference);
    }
    else
        mmsValue->value.structure.components[2] = MmsValue_newVisibleString("");

    /* OldEntrTm */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("OldEntrTm");
    namedVariable->type = MMS_BINARY_TIME;
    namedVariable->typeSpec.binaryTime = 6;
    rcb->typeSpec.structure.elements[3] = namedVariable;

    mmsValue->value.structure.components[3] = MmsValue_newBinaryTime(false);

    /* NewEntrTm */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("NewEntrTm");
    namedVariable->type = MMS_BINARY_TIME;
    namedVariable->typeSpec.binaryTime = 6;
    rcb->typeSpec.structure.elements[4] = namedVariable;

    mmsValue->value.structure.components[4] = MmsValue_newBinaryTime(false);

    /* OldEntr */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("OldEntr");
    namedVariable->type = MMS_OCTET_STRING;
    namedVariable->typeSpec.octetString = 8;

    rcb->typeSpec.structure.elements[5] = namedVariable;

    mmsValue->value.structure.components[5] = MmsValue_newOctetString(8, 8);

    /* NewEntr */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("NewEntr");
    namedVariable->type = MMS_OCTET_STRING;
    namedVariable->typeSpec.octetString = 8;

    rcb->typeSpec.structure.elements[6] = namedVariable;

    mmsValue->value.structure.components[6] = MmsValue_newOctetString(8, 8);

    /* TrgOps */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("TrgOps");
    namedVariable->type = MMS_BIT_STRING;
    namedVariable->typeSpec.bitString = -6;
    rcb->typeSpec.structure.elements[7] = namedVariable;
    mmsValue->value.structure.components[7] = createTrgOps(logControlBlock);

    /* IntgPd */
    namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1, sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("IntgPd");
    namedVariable->type = MMS_UNSIGNED;
    namedVariable->typeSpec.unsignedInteger = 32;
    rcb->typeSpec.structure.elements[8] = namedVariable;
    mmsValue->value.structure.components[8] =
            MmsValue_newUnsignedFromUint32(logControlBlock->intPeriod);


    //TODO logControl->rcbValues = mmsValue;

    return rcb;
}



MmsVariableSpecification*
Logging_createLCBs(MmsMapping* self, MmsDomain* domain, LogicalNode* logicalNode,
        int lcbCount)
{
    MmsVariableSpecification* namedVariable = (MmsVariableSpecification*) GLOBAL_CALLOC(1,
            sizeof(MmsVariableSpecification));
    namedVariable->name = copyString("LG");
    namedVariable->type = MMS_STRUCTURE;

    namedVariable->typeSpec.structure.elementCount = lcbCount;
    namedVariable->typeSpec.structure.elements = (MmsVariableSpecification**) GLOBAL_CALLOC(lcbCount,
            sizeof(MmsVariableSpecification*));

    int currentLcb = 0;

    while (currentLcb < lcbCount) {

        LogControl* logControl = LogControl_create(logicalNode);

        LogControlBlock* logControlBlock = getLCBForLogicalNodeWithIndex(self, logicalNode, currentLcb);

        logControl->name = createString(3, logicalNode->name, "$LG$", logControlBlock->name);
        logControl->domain = domain;

        namedVariable->typeSpec.structure.elements[currentLcb] =
                createLogControlBlock(logControlBlock, logControl);

        LinkedList_add(self->logControls, logControl);

        currentLcb++;
    }

    return namedVariable;
}


