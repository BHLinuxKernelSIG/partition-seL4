#include "shared.h"

#include "../process/process.h"
#include "../process/thread.h"
#include "../process/pid.h"

#include "../../common.h"
#include "../../state.h"

char *create_shared_mem(int size)
{
    procServ.shared_dspace = ram_dspace_create(&procServ.dspaceList, size);

    seL4_CPtr frame = procServ.shared_dspace->pages[0].cptr;
    char* addr = (char*) vspace_map_pages(&procServ.vspace, &frame, NULL, 
                                        seL4_AllRights, 1,
                                        seL4_PageBits, true);
    return addr;
}