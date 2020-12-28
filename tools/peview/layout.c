/*
 * Process Hacker -
 *   PE viewer
 *
 * Copyright (C) 2020 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <peview.h>
#include <cryptuiapi.h>
#include "colmgr.h"

#define WM_PV_LAYOUT_CONTEXTMENU (WM_APP + 801)

typedef struct _PV_PE_LAYOUT_CONTEXT
{
    HWND WindowHandle;
    HWND TreeNewHandle;
    ULONG TreeNewSortColumn;
    PH_SORT_ORDER TreeNewSortOrder;
    PPH_HASHTABLE NodeHashtable;
    PPH_LIST NodeList;
    PPH_LIST NodeRootList;
    PPH_STRING StatusMessage;
} PV_PE_LAYOUT_CONTEXT, *PPV_PE_LAYOUT_CONTEXT;

typedef enum _PV_LAYOUT_TREE_COLUMN_NAME
{
    PV_LAYOUT_TREE_COLUMN_NAME_NAME,
    PV_LAYOUT_TREE_COLUMN_NAME_VALUE,
    PV_LAYOUT_TREE_COLUMN_NAME_MAXIMUM
} PV_LAYOUT_TREE_COLUMN_NAME;

typedef struct _PV_LAYOUT_NODE
{
    PH_TREENEW_NODE Node;

    ULONG64 UniqueId;
    PPH_STRING Name;
    PPH_STRING Value;

    struct _PV_LAYOUT_NODE* Parent;
    PPH_LIST Children;

    PH_STRINGREF TextCache[PV_LAYOUT_TREE_COLUMN_NAME_MAXIMUM];
} PV_LAYOUT_NODE, *PPV_LAYOUT_NODE;

BOOLEAN PvLayoutNodeHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );
ULONG PvLayoutNodeHashtableHashFunction(
    _In_ PVOID Entry
    );
VOID PvDestroyLayoutNode(
    _In_ PPV_LAYOUT_NODE CertificateNode
    );
BOOLEAN NTAPI PvLayoutTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    );

BOOLEAN PhInsertCopyCellEMenuItem(
    _In_ struct _PH_EMENU_ITEM* Menu,
    _In_ ULONG InsertAfterId,
    _In_ HWND TreeNewHandle,
    _In_ PPH_TREENEW_COLUMN Column
    );

BOOLEAN PhHandleCopyCellEMenuItem(
    _In_ struct _PH_EMENU_ITEM* SelectedItem
    );

VOID PvInitializeLayoutTree(
    _In_ PPV_PE_LAYOUT_CONTEXT Context
    )
{
    PPH_STRING settings;

    Context->NodeHashtable = PhCreateHashtable(
        sizeof(PV_LAYOUT_NODE),
        PvLayoutNodeHashtableEqualFunction,
        PvLayoutNodeHashtableHashFunction,
        100
        );
    Context->NodeList = PhCreateList(10);
    Context->NodeRootList = PhCreateList(10);

    PhSetControlTheme(Context->TreeNewHandle, L"explorer");
    TreeNew_SetCallback(Context->TreeNewHandle, PvLayoutTreeNewCallback, Context);

    PhAddTreeNewColumn(Context->TreeNewHandle, PV_LAYOUT_TREE_COLUMN_NAME_NAME, TRUE, L"Name", 200, PH_ALIGN_LEFT, 0, 0);
    PhAddTreeNewColumn(Context->TreeNewHandle, PV_LAYOUT_TREE_COLUMN_NAME_VALUE, TRUE, L"Value", 800, PH_ALIGN_LEFT, 1, 0);

    //TreeNew_SetTriState(Context->TreeNewHandle, TRUE);
    //TreeNew_SetSort(Context->TreeNewHandle, PV_LAYOUT_TREE_COLUMN_NAME_NAME, NoSortOrder);
    //TreeNew_SetRowHeight(Context->TreeNewHandle, 22);

    settings = PhGetStringSetting(L"ImageLayoutTreeColumns");
    PhCmLoadSettings(Context->TreeNewHandle, &settings->sr);
    PhDereferenceObject(settings);
}

VOID PvDeleteLayoutTree(
    _In_ PPV_PE_LAYOUT_CONTEXT Context
    )
{
    PPH_STRING settings;
    ULONG i;

    settings = PhCmSaveSettings(Context->TreeNewHandle);
    PhSetStringSetting2(L"ImageLayoutTreeColumns", &settings->sr);
    PhDereferenceObject(settings);

    for (i = 0; i < Context->NodeList->Count; i++)
        PvDestroyLayoutNode(Context->NodeList->Items[i]);

    PhDereferenceObject(Context->NodeHashtable);
    PhDereferenceObject(Context->NodeList);
    PhDereferenceObject(Context->NodeRootList);
}

BOOLEAN PvLayoutNodeHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PPV_LAYOUT_NODE windowNode1 = *(PPV_LAYOUT_NODE*)Entry1;
    PPV_LAYOUT_NODE windowNode2 = *(PPV_LAYOUT_NODE*)Entry2;

    return windowNode1->UniqueId == windowNode2->UniqueId;
}

ULONG PvLayoutNodeHashtableHashFunction(
    _In_ PVOID Entry
    )
{
    return PhHashInt64((ULONG_PTR)(*(PPV_LAYOUT_NODE*)Entry)->UniqueId);
}

PPV_LAYOUT_NODE PvAddLayoutNode(
    _Inout_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_ PWSTR Name,
    _In_ PPH_STRING Value
    )
{
    static ULONG64 index = 0;
    PPV_LAYOUT_NODE windowNode;

    windowNode = PhAllocateZero(sizeof(PV_LAYOUT_NODE));
    windowNode->UniqueId = ++index;
    PhInitializeTreeNewNode(&windowNode->Node);

    memset(windowNode->TextCache, 0, sizeof(PH_STRINGREF) * PV_LAYOUT_TREE_COLUMN_NAME_MAXIMUM);
    windowNode->Node.TextCache = windowNode->TextCache;
    windowNode->Node.TextCacheSize = PV_LAYOUT_TREE_COLUMN_NAME_MAXIMUM;

    windowNode->Name = PhCreateString(Name);
    windowNode->Value = Value;
    windowNode->Children = PhCreateList(1);

    PhAddEntryHashtable(Context->NodeHashtable, &windowNode);
    PhAddItemList(Context->NodeList, windowNode);

    //if (Context->FilterSupport.FilterList)
    //   windowNode->Node.Visible = PhApplyTreeNewFiltersToNode(&Context->FilterSupport, &windowNode->Node);

    return windowNode;
}

PPV_LAYOUT_NODE PvAddChildLayoutNode(
    _In_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_opt_ PPV_LAYOUT_NODE ParentNode,
    _In_ PWSTR Name,
    _In_ PPH_STRING Value
    )
{
    PPV_LAYOUT_NODE childNode;

    childNode = PvAddLayoutNode(Context, Name, Value);

    if (ParentNode)
    {
        // This is a child node.
        childNode->Node.Expanded = TRUE;
        childNode->Parent = ParentNode;

        PhAddItemList(ParentNode->Children, childNode);
    }
    else
    {
        // This is a root node.
        childNode->Node.Expanded = TRUE;
        PhAddItemList(Context->NodeRootList, childNode);
    }

    return childNode;
}

PPV_LAYOUT_NODE PvFindLayoutNode(
    _In_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_ ULONG UniqueId
    )
{
    PV_LAYOUT_NODE lookupWindowNode;
    PPV_LAYOUT_NODE lookupWindowNodePtr = &lookupWindowNode;
    PPV_LAYOUT_NODE* windowNode;

    lookupWindowNode.Node.Index = UniqueId;

    windowNode = (PPV_LAYOUT_NODE*)PhFindEntryHashtable(
        Context->NodeHashtable,
        &lookupWindowNodePtr
        );

    if (windowNode)
        return *windowNode;
    else
        return NULL;
}

VOID PvRemoveLayoutNode(
    _In_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_ PPV_LAYOUT_NODE WindowNode
    )
{
    ULONG index;

    // Remove from hashtable/list and cleanup.

    PhRemoveEntryHashtable(Context->NodeHashtable, &WindowNode);

    if ((index = PhFindItemList(Context->NodeList, WindowNode)) != ULONG_MAX)
        PhRemoveItemList(Context->NodeList, index);

    PvDestroyLayoutNode(WindowNode);

    TreeNew_NodesStructured(Context->TreeNewHandle);
}

VOID PvDestroyLayoutNode(
    _In_ PPV_LAYOUT_NODE WindowNode
    )
{
    PhDereferenceObject(WindowNode->Children);

    if (WindowNode->Name) PhDereferenceObject(WindowNode->Name);
    if (WindowNode->Value) PhDereferenceObject(WindowNode->Value);

    PhFree(WindowNode);
}

#define SORT_FUNCTION(Column) PvLayoutTreeNewCompare##Column
#define BEGIN_SORT_FUNCTION(Column) static int __cdecl PvLayoutTreeNewCompare##Column( \
    _In_ void *_context, \
    _In_ const void *_elem1, \
    _In_ const void *_elem2 \
    ) \
{ \
    PPV_LAYOUT_NODE node1 = *(PPV_LAYOUT_NODE *)_elem1; \
    PPV_LAYOUT_NODE node2 = *(PPV_LAYOUT_NODE *)_elem2; \
    int sortResult = 0;

#define END_SORT_FUNCTION \
    return PhModifySort(sortResult, ((PPV_PE_LAYOUT_CONTEXT)_context)->TreeNewSortOrder); \
}

BEGIN_SORT_FUNCTION(Name)
{
    sortResult = PhCompareString(node1->Name, node2->Name, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Index)
{
    sortResult = uintptrcmp((ULONG_PTR)node1->Node.Index, (ULONG_PTR)node2->Node.Index);
}
END_SORT_FUNCTION

BOOLEAN NTAPI PvLayoutTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    )
{
    PPV_PE_LAYOUT_CONTEXT context = Context;
    PPV_LAYOUT_NODE node;

    if (!context)
        return FALSE;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;

            if (!getChildren)
                break;

            node = (PPV_LAYOUT_NODE)getChildren->Node;

            if (!node)
            {
                getChildren->Children = (PPH_TREENEW_NODE*)context->NodeRootList->Items;
                getChildren->NumberOfChildren = context->NodeRootList->Count;
            }
            else
            {
                getChildren->Children = (PPH_TREENEW_NODE*)node->Children->Items;
                getChildren->NumberOfChildren = node->Children->Count;
            }
        }
        return TRUE;
    case TreeNewIsLeaf:
        {
            PPH_TREENEW_IS_LEAF isLeaf = Parameter1;

            if (!isLeaf)
                break;

            node = (PPV_LAYOUT_NODE)isLeaf->Node;

            if (context->TreeNewSortOrder == NoSortOrder)
                isLeaf->IsLeaf = !(node->Children && node->Children->Count);
            else
                isLeaf->IsLeaf = TRUE;
        }
        return TRUE;
    case TreeNewGetCellText:
        {
            PPH_TREENEW_GET_CELL_TEXT getCellText = Parameter1;

            if (!getCellText)
                break;

            node = (PPV_LAYOUT_NODE)getCellText->Node;

            switch (getCellText->Id)
            {
            case PV_LAYOUT_TREE_COLUMN_NAME_NAME:
                {
                    getCellText->Text = PhGetStringRef(node->Name);
                }
                break;
            case PV_LAYOUT_TREE_COLUMN_NAME_VALUE:
                {
                    getCellText->Text = PhGetStringRef(node->Value);
                }
                break;
            default:
                return FALSE;
            }

            getCellText->Flags = TN_CACHE;
        }
        return TRUE;
    case TreeNewGetNodeColor:
        {
            PPH_TREENEW_GET_NODE_COLOR getNodeColor = Parameter1;

            if (!getNodeColor)
                break;

            node = (PPV_LAYOUT_NODE)getNodeColor->Node;

            getNodeColor->Flags = TN_AUTO_FORECOLOR | TN_CACHE;
        }
        return TRUE;
    case TreeNewSortChanged:
        {
            TreeNew_GetSort(hwnd, &context->TreeNewSortColumn, &context->TreeNewSortOrder);
            // Force a rebuild to sort the items.
            TreeNew_NodesStructured(hwnd);
        }
        return TRUE;
    case TreeNewKeyDown:
        {
            PPH_TREENEW_KEY_EVENT keyEvent = Parameter1;

            if (!keyEvent)
                break;

            switch (keyEvent->VirtualKey)
            {
            case 'C':
                //if (GetKeyState(VK_CONTROL) < 0)
                    //SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_WINDOW_COPY, 0);
                break;
            }
        }
        return TRUE;
    case TreeNewLeftDoubleClick:
        {
            //SendMessage(context->WindowHandle, WM_COMMAND, PROPERTIES, 0);
        }
        return TRUE;
    case TreeNewContextMenu:
        {
            PPH_TREENEW_CONTEXT_MENU contextMenuEvent = Parameter1;

            SendMessage(context->WindowHandle, WM_COMMAND, WM_PV_LAYOUT_CONTEXTMENU, (LPARAM)contextMenuEvent);
        }
        return TRUE;
    case TreeNewHeaderRightClick:
        {
            //PH_TN_COLUMN_MENU_DATA data;

            //data.TreeNewHandle = hwnd;
            //data.MouseEvent = Parameter1;
            //data.DefaultSortColumn = 0;
            //data.DefaultSortOrder = AscendingSortOrder;
            //PhInitializeTreeNewColumnMenu(&data);

            //data.Selection = PhShowEMenu(data.Menu, hwnd, PH_EMENU_SHOW_LEFTRIGHT,
            //    PH_ALIGN_LEFT | PH_ALIGN_TOP, data.MouseEvent->ScreenLocation.x, data.MouseEvent->ScreenLocation.y);
            //PhHandleTreeNewColumnMenu(&data);
            //PhDeleteTreeNewColumnMenu(&data);
        }
        return TRUE;
    }

    return FALSE;
}

VOID PvClearLayoutTree(
    _In_ PPV_PE_LAYOUT_CONTEXT Context
    )
{
    for (ULONG i = 0; i < Context->NodeList->Count; i++)
        PvDestroyLayoutNode(Context->NodeList->Items[i]);

    PhClearHashtable(Context->NodeHashtable);
    PhClearList(Context->NodeList);
    PhClearList(Context->NodeRootList);
}

PPV_LAYOUT_NODE PvGetSelectedLayoutNode(
    _In_ PPV_PE_LAYOUT_CONTEXT Context
    )
{
    PPV_LAYOUT_NODE windowNode = NULL;
    ULONG i;

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        windowNode = Context->NodeList->Items[i];

        if (windowNode->Node.Selected)
            return windowNode;
    }

    return NULL;
}

VOID PvGetSelectedLayoutNodes(
    _In_ PPV_PE_LAYOUT_CONTEXT Context,
    _Out_ PPV_LAYOUT_NODE**Windows,
    _Out_ PULONG NumberOfWindows
    )
{
    PPH_LIST list;
    ULONG i;

    list = PhCreateList(2);

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        PPV_LAYOUT_NODE node = Context->NodeList->Items[i];

        if (node->Node.Selected)
        {
            PhAddItemList(list, node);
        }
    }

    *Windows = PhAllocateCopy(list->Items, sizeof(PVOID) * list->Count);
    *NumberOfWindows = list->Count;

    PhDereferenceObject(list);
}

VOID PvExpandAllLayoutNodes(
    _In_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_ BOOLEAN Expand
    )
{
    ULONG i;
    BOOLEAN needsRestructure = FALSE;

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        PPV_LAYOUT_NODE node = Context->NodeList->Items[i];

        if (node->Children->Count != 0 && node->Node.Expanded != Expand)
        {
            node->Node.Expanded = Expand;
            needsRestructure = TRUE;
        }
    }

    if (needsRestructure)
        TreeNew_NodesStructured(Context->TreeNewHandle);
}

VOID PvDeselectAllLayoutNodes(
    _In_ PPV_PE_LAYOUT_CONTEXT Context
    )
{
    TreeNew_DeselectRange(Context->TreeNewHandle, 0, -1);
}

VOID PvSelectAndEnsureVisibleLayoutNodes(
    _In_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_ PPV_LAYOUT_NODE* CertificateNodes,
    _In_ ULONG NumberOfCertificateNodes
    )
{
    ULONG i;
    PPV_LAYOUT_NODE leader = NULL;
    PPV_LAYOUT_NODE node;
    BOOLEAN needsRestructure = FALSE;

    PvDeselectAllLayoutNodes(Context);

    for (i = 0; i < NumberOfCertificateNodes; i++)
    {
        if (CertificateNodes[i]->Node.Visible)
        {
            leader = CertificateNodes[i];
            break;
        }
    }

    if (!leader)
        return;

    // Expand recursively upwards, and select the nodes.

    for (i = 0; i < NumberOfCertificateNodes; i++)
    {
        if (!CertificateNodes[i]->Node.Visible)
            continue;

        node = CertificateNodes[i]->Parent;

        while (node)
        {
            if (!node->Node.Expanded)
                needsRestructure = TRUE;

            node->Node.Expanded = TRUE;
            node = node->Parent;
        }

        CertificateNodes[i]->Node.Selected = TRUE;
    }

    if (needsRestructure)
        TreeNew_NodesStructured(Context->TreeNewHandle);

    TreeNew_SetFocusNode(Context->TreeNewHandle, &leader->Node);
    TreeNew_SetMarkNode(Context->TreeNewHandle, &leader->Node);
    TreeNew_EnsureVisible(Context->TreeNewHandle, &leader->Node);
    TreeNew_InvalidateNode(Context->TreeNewHandle, &leader->Node);
}

PPH_STRING PvLayoutPeFormatDateTime(
    _In_ PSYSTEMTIME SystemTime
    )
{
    return PhFormatString(
        L"%s, %s\n",
        PH_AUTO_T(PH_STRING, PhFormatDate(SystemTime, L"dddd, MMMM d, yyyy"))->Buffer,
        PH_AUTO_T(PH_STRING, PhFormatTime(SystemTime, L"hh:mm:ss tt"))->Buffer
        );
}

PPH_STRING PvLayoutGetRelativeTimeString(
    _In_ PLARGE_INTEGER Time
    )
{
    LARGE_INTEGER time;
    LARGE_INTEGER currentTime;
    SYSTEMTIME timeFields;
    PPH_STRING timeRelativeString;
    PPH_STRING timeString;

    time = *Time;
    PhQuerySystemTime(&currentTime);
    timeRelativeString = PH_AUTO(PhFormatTimeSpanRelative(currentTime.QuadPart - time.QuadPart));

    PhLargeIntegerToLocalSystemTime(&timeFields, &time);
    timeString = PH_AUTO(PvLayoutPeFormatDateTime(&timeFields));

    return PhFormatString(L"%s (%s ago)", timeString->Buffer, timeRelativeString->Buffer);
}

PWSTR PvLayoutNameFlagsToString(
    _In_ ULONG Flags
    )
{
    if (Flags == 0)
    {
        return L"HLINK Name";
    }

    if (Flags & FILE_LAYOUT_NAME_ENTRY_PRIMARY)
    {
        return L"NTFS Name";
    }

    if (Flags & FILE_LAYOUT_NAME_ENTRY_DOS)
    {
        return L"DOS Name";
    }

    return L"UNKNOWN";
}

PPH_STRING PvLayoutSteamFlagsToString(
    _In_ ULONG Flags
    )
{
    PH_STRING_BUILDER stringBuilder;
    WCHAR pointer[PH_PTR_STR_LEN_1];

    PhInitializeStringBuilder(&stringBuilder, 10);

    if (Flags & STREAM_LAYOUT_ENTRY_IMMOVABLE)
        PhAppendStringBuilder2(&stringBuilder, L"Immovable, ");
    if (Flags & STREAM_LAYOUT_ENTRY_PINNED)
        PhAppendStringBuilder2(&stringBuilder, L"Pinned, ");
    if (Flags & STREAM_LAYOUT_ENTRY_RESIDENT)
        PhAppendStringBuilder2(&stringBuilder, L"Resident, ");
    if (Flags & STREAM_LAYOUT_ENTRY_NO_CLUSTERS_ALLOCATED)
        PhAppendStringBuilder2(&stringBuilder, L"No clusters allocated, ");
    if (Flags & STREAM_LAYOUT_ENTRY_HAS_INFORMATION)
        PhAppendStringBuilder2(&stringBuilder, L"Has parsed information, ");

    if (PhEndsWithString2(stringBuilder.String, L", ", FALSE))
        PhRemoveEndStringBuilder(&stringBuilder, 2);

    PhPrintPointer(pointer, UlongToPtr(Flags));
    PhAppendFormatStringBuilder(&stringBuilder, L" (%s)", pointer);

    return PhFinalStringBuilderString(&stringBuilder);
}

PPH_STRING PvLayoutGetParentIdName(
    _In_ HANDLE FileHandle,
    _In_ LONGLONG ParentFileId)
{
    PPH_STRING parentName = NULL;
    NTSTATUS status;
    HANDLE linkHandle;
    UNICODE_STRING fileNameUs;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK isb;

    fileNameUs.Length = sizeof(LONGLONG);
    fileNameUs.MaximumLength = sizeof(LONGLONG);
    fileNameUs.Buffer = (PWSTR)&ParentFileId;

    InitializeObjectAttributes(
        &oa,
        &fileNameUs,
        OBJ_CASE_INSENSITIVE,
        FileHandle,
        NULL
        );

    status = NtCreateFile(
        &linkHandle,
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &oa,
        &isb,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_BY_FILE_ID,
        NULL,
        0
        );

    if (NT_SUCCESS(status))
    {
        status = PhGetFileHandleName(linkHandle, &parentName);
        NtClose(linkHandle);
    }

    return parentName;
}

VOID PvLayoutSetStatusMessage(
    _Inout_ PPV_PE_LAYOUT_CONTEXT Context,
    _In_ ULONG Status
    )
{
    PPH_STRING statusMessage;

    statusMessage = PhGetStatusMessage(Status, 0);
    PhMoveReference(&Context->StatusMessage, PhConcatStrings2(
        L"Unable to query file layout information:\n",
        PhGetStringOrDefault(statusMessage, L"Unknown error.")
        ));
    TreeNew_SetEmptyText(Context->TreeNewHandle, &Context->StatusMessage->sr, 0);
    PhClearReference(&statusMessage);
}

#define FILE_LAYOUT_ENTRY_VERSION 0x1
#define STREAM_LAYOUT_ENTRY_VERSION 0x1
#define FIRST_LAYOUT_ENTRY(LayoutEntry) ((LayoutEntry) ? PTR_ADD_OFFSET(LayoutEntry, (LayoutEntry)->FirstFileOffset) : NULL)
#define NEXT_LAYOUT_ENTRY(LayoutEntry) (((LayoutEntry))->NextFileOffset ? PTR_ADD_OFFSET((LayoutEntry), (LayoutEntry)->NextFileOffset) : NULL)

typedef enum _FILE_METADATA_OPTIMIZATION_STATE
{
    FileMetadataOptimizationNone = 0,
    FileMetadataOptimizationInProgress,
    FileMetadataOptimizationPending
} FILE_METADATA_OPTIMIZATION_STATE, *PFILE_METADATA_OPTIMIZATION_STATE;

typedef struct _FILE_QUERY_METADATA_OPTIMIZATION_OUTPUT
{
    FILE_METADATA_OPTIMIZATION_STATE State;
    ULONG AttributeListSize;
    ULONG MetadataSpaceUsed;
    ULONG MetadataSpaceAllocated;
    ULONG NumberOfFileRecords;
    ULONG NumberOfResidentAttributes;
    ULONG NumberOfNonresidentAttributes;
    ULONG TotalInProgress;
    ULONG TotalPending;
} FILE_QUERY_METADATA_OPTIMIZATION_OUTPUT, *PFILE_QUERY_METADATA_OPTIMIZATION_OUTPUT;

NTSTATUS PvLayoutGetMetadataOptimization(
    _Out_ PFILE_QUERY_METADATA_OPTIMIZATION_OUTPUT FileMetadataOptimization
    )
{
    NTSTATUS status;
    HANDLE fileHandle;
    IO_STATUS_BLOCK isb;

    status = PhCreateFileWin32(
        &fileHandle,
        PhGetString(PvFileName),
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
        return status;

    status = NtFsControlFile(
        fileHandle,
        NULL,
        NULL,
        NULL,
        &isb,
        FSCTL_QUERY_FILE_METADATA_OPTIMIZATION,
        NULL,
        0,
        FileMetadataOptimization,
        sizeof(FILE_QUERY_METADATA_OPTIMIZATION_OUTPUT)
        );

    NtClose(fileHandle);

    return status;
}

NTSTATUS PvLayoutSetMetadataOptimization(
    VOID
    )
{
    NTSTATUS status;
    HANDLE fileHandle;
    HANDLE tokenHandle;
    IO_STATUS_BLOCK isb;

    status = PhCreateFileWin32(
        &fileHandle,
        PhGetString(PvFileName),
        FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (NT_SUCCESS(status))
    {
        status = NtFsControlFile(
            fileHandle,
            NULL,
            NULL,
            NULL,
            &isb,
            FSCTL_INITIATE_FILE_METADATA_OPTIMIZATION,
            NULL,
            0,
            NULL,
            0
            );

        NtClose(fileHandle);
    }

    if (status == STATUS_SUCCESS)
        return status;

    status = PhOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tokenHandle);

    if (!NT_SUCCESS(status))
        return status;

    if (!PhSetTokenPrivilege2(tokenHandle, SE_MANAGE_VOLUME_PRIVILEGE, SE_PRIVILEGE_ENABLED))
    {
        NtClose(tokenHandle);
        return STATUS_UNSUCCESSFUL;
    }

    status = PhCreateFileWin32(
        &fileHandle,
        PhGetString(PvFileName),
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (NT_SUCCESS(status))
    {
        status = NtFsControlFile(
            fileHandle,
            NULL,
            NULL,
            NULL,
            &isb,
            FSCTL_INITIATE_FILE_METADATA_OPTIMIZATION,
            NULL,
            0,
            NULL,
            0
            );

        NtClose(fileHandle);
    }

    PhSetTokenPrivilege2(tokenHandle, SE_MANAGE_VOLUME_PRIVILEGE, 0);
    NtClose(tokenHandle);

    return status;
}

NTSTATUS PvGetFileAllocatedRanges(
    _In_ HANDLE FileHandle,
    _In_ PV_FILE_ALLOCATION_CALLBACK Callback,
    _In_ PVOID Context
    )
{
    NTSTATUS status;
    ULONG outputCount;
    ULONG outputLength;
    LARGE_INTEGER fileSize;
    FILE_ALLOCATED_RANGE_BUFFER input;
    PFILE_ALLOCATED_RANGE_BUFFER output;
    IO_STATUS_BLOCK isb;

    status = PhGetFileSize(FileHandle, &fileSize);

    if (!NT_SUCCESS(status))
        return status;

    memset(&input, 0, sizeof(FILE_ALLOCATED_RANGE_BUFFER));
    input.FileOffset.QuadPart = 0;
    input.Length.QuadPart = fileSize.QuadPart;

    outputLength = sizeof(FILE_ALLOCATED_RANGE_BUFFER) * PAGE_SIZE;
    output = PhAllocateZero(outputLength);

    while (TRUE)
    {
        status = NtFsControlFile(
            FileHandle,
            NULL,
            NULL,
            NULL,
            &isb,
            FSCTL_QUERY_ALLOCATED_RANGES,
            &input,
            sizeof(FILE_ALLOCATED_RANGE_BUFFER),
            output,
            outputLength
            );

        if (!NT_SUCCESS(status))
            break;

        outputCount = (ULONG)isb.Information / sizeof(FILE_ALLOCATED_RANGE_BUFFER);

        if (outputCount == 0)
            break;

        for (ULONG i = 0; i < outputCount; i++)
        {
            if (Callback)
            {
                if (!Callback(&output[i], Context))
                    break;
            }
        }

        input.FileOffset.QuadPart = output[outputCount - 1].FileOffset.QuadPart + output[outputCount - 1].Length.QuadPart;
        input.Length.QuadPart = fileSize.QuadPart - input.FileOffset.QuadPart;

        if (input.Length.QuadPart == 0)
            break;
    }

    return status;
}

NTSTATUS PvLayoutEnumerateFileLayouts(
    _In_ PPV_PE_LAYOUT_CONTEXT Context
    )
{
    NTSTATUS status;
    HANDLE fileHandle = NULL;
    HANDLE volumeHandle = NULL;
    PPH_STRING volumeName;
    PH_STRINGREF firstPart;
    PH_STRINGREF lastPart;
    IO_STATUS_BLOCK isb;
    ULONG outputLength;
    FILE_INTERNAL_INFORMATION fileInternalInfo;
    FILE_QUERY_METADATA_OPTIMIZATION_OUTPUT fileMetadataOptimization;
    QUERY_FILE_LAYOUT_INPUT input;
    PQUERY_FILE_LAYOUT_OUTPUT output;
    PFILE_LAYOUT_ENTRY fileLayoutEntry;
    PFILE_LAYOUT_NAME_ENTRY fileLayoutNameEntry;
    PSTREAM_LAYOUT_ENTRY fileLayoutSteamEntry;
    PFILE_LAYOUT_INFO_ENTRY fileLayoutInfoEntry;

    if (!PhSplitStringRefAtChar(&PvFileName->sr, L':', &firstPart, &lastPart))
        return STATUS_UNSUCCESSFUL;
    if (firstPart.Length != sizeof(L':'))
        return STATUS_UNSUCCESSFUL;

    //WCHAR volumeName[MAX_PATH + 1];
    //if (!GetVolumePathName(PvFileName->Buffer, volumeName, MAX_PATH))
    //    return PhGetLastWin32ErrorAsNtStatus();

    volumeName = PhCreateString2(&firstPart);
    PhMoveReference(&volumeName, PhConcatStrings(3, L"\\??\\", PhGetStringOrEmpty(volumeName), L":"));

    if (PhDetermineDosPathNameType(PhGetString(volumeName)) != RtlPathTypeRooted)
    {
        PhDereferenceObject(volumeName);
        return STATUS_UNSUCCESSFUL;
    }

    status = PhCreateFileWin32(
        &fileHandle,
        PhGetString(PvFileName),
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    memset(&fileMetadataOptimization, 0, sizeof(FILE_QUERY_METADATA_OPTIMIZATION_OUTPUT));
    NtFsControlFile(
        fileHandle,
        NULL,
        NULL,
        NULL,
        &isb,
        FSCTL_QUERY_FILE_METADATA_OPTIMIZATION,
        NULL,
        0,
        &fileMetadataOptimization,
        sizeof(FILE_QUERY_METADATA_OPTIMIZATION_OUTPUT)
        );

    status = NtQueryInformationFile(
        fileHandle,
        &isb,
        &fileInternalInfo,
        sizeof(FILE_INTERNAL_INFORMATION),
        FileInternalInformation
        );

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    status = PhCreateFile(
        &volumeHandle,
        PhGetString(volumeName),
        FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, // magic value
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
        goto CleanupExit;

    memset(&input, 0, sizeof(QUERY_FILE_LAYOUT_INPUT));
    input.Flags =
        QUERY_FILE_LAYOUT_RESTART |
        QUERY_FILE_LAYOUT_INCLUDE_NAMES |
        QUERY_FILE_LAYOUT_INCLUDE_STREAMS |
        QUERY_FILE_LAYOUT_INCLUDE_EXTENTS |
        QUERY_FILE_LAYOUT_INCLUDE_EXTRA_INFO |
        QUERY_FILE_LAYOUT_INCLUDE_STREAMS_WITH_NO_CLUSTERS_ALLOCATED |
        QUERY_FILE_LAYOUT_INCLUDE_FULL_PATH_IN_NAMES |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_DSC_ATTRIBUTE |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_TXF_ATTRIBUTE |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_EFS_ATTRIBUTE |
        QUERY_FILE_LAYOUT_INCLUDE_FILES_WITH_DSC_ATTRIBUTE |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_DATA_ATTRIBUTE |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_REPARSE_ATTRIBUTE |
        QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_EA_ATTRIBUTE;

    input.FilterEntryCount = 1;
    input.FilterType = QUERY_FILE_LAYOUT_FILTER_TYPE_FILEID;
    input.Filter.FileReferenceRanges->StartingFileReferenceNumber = fileInternalInfo.IndexNumber.QuadPart;
    input.Filter.FileReferenceRanges->EndingFileReferenceNumber = fileInternalInfo.IndexNumber.QuadPart;

    outputLength = 0x2000000; // magic value
    output = PhAllocateZero(outputLength);

    while (TRUE)
    {
        status = NtFsControlFile(
            volumeHandle,
            NULL,
            NULL,
            NULL,
            &isb,
            FSCTL_QUERY_FILE_LAYOUT,
            &input,
            sizeof(QUERY_FILE_LAYOUT_INPUT),
            output,
            outputLength
            );

        if (!NT_SUCCESS(status))
            break;

        for (fileLayoutEntry = FIRST_LAYOUT_ENTRY(output); fileLayoutEntry; fileLayoutEntry = NEXT_LAYOUT_ENTRY(fileLayoutEntry))
        {
            if (fileLayoutEntry->Version != FILE_LAYOUT_ENTRY_VERSION)
            {
                status = STATUS_INVALID_KERNEL_INFO_VERSION;
                break;
            }

            fileLayoutNameEntry = PTR_ADD_OFFSET(fileLayoutEntry, fileLayoutEntry->FirstNameOffset);
            fileLayoutSteamEntry = PTR_ADD_OFFSET(fileLayoutEntry, fileLayoutEntry->FirstStreamOffset);
            fileLayoutInfoEntry = PTR_ADD_OFFSET(fileLayoutEntry, fileLayoutEntry->ExtraInfoOffset);

            PvAddChildLayoutNode(Context, NULL, L"File reference number", PhFormatString(L"%I64u (0x%I64x)", fileLayoutEntry->FileReferenceNumber, fileLayoutEntry->FileReferenceNumber));
            PvAddChildLayoutNode(Context, NULL, L"File attributes", PhFormatUInt64(fileLayoutEntry->FileAttributes, FALSE));
            PvAddChildLayoutNode(Context, NULL, L"File entry flags", PhFormatUInt64(fileLayoutEntry->Flags, FALSE));
            PvAddChildLayoutNode(Context, NULL, L"Creation time", PvLayoutGetRelativeTimeString(&fileLayoutInfoEntry->BasicInformation.CreationTime));
            PvAddChildLayoutNode(Context, NULL, L"Last access time", PvLayoutGetRelativeTimeString(&fileLayoutInfoEntry->BasicInformation.LastAccessTime));
            PvAddChildLayoutNode(Context, NULL, L"Last write time", PvLayoutGetRelativeTimeString(&fileLayoutInfoEntry->BasicInformation.LastWriteTime));
            PvAddChildLayoutNode(Context, NULL, L"Change time", PvLayoutGetRelativeTimeString(&fileLayoutInfoEntry->BasicInformation.ChangeTime));
            PvAddChildLayoutNode(Context, NULL, L"LastUsn", PhFormatUInt64(fileLayoutInfoEntry->Usn, TRUE));
            PvAddChildLayoutNode(Context, NULL, L"OwnerId", PhFormatUInt64(fileLayoutInfoEntry->OwnerId, FALSE));
            PvAddChildLayoutNode(Context, NULL, L"SecurityId", PhFormatUInt64(fileLayoutInfoEntry->SecurityId, FALSE));
            PvAddChildLayoutNode(Context, NULL, L"StorageReserveId", PhFormatUInt64(fileLayoutInfoEntry->StorageReserveId, FALSE));
            PvAddChildLayoutNode(Context, NULL, L"Attribute list size", PhFormatSize(fileMetadataOptimization.AttributeListSize, ULONG_MAX));
            PvAddChildLayoutNode(Context, NULL, L"Metadata space used", PhFormatSize(fileMetadataOptimization.MetadataSpaceUsed, ULONG_MAX));
            PvAddChildLayoutNode(Context, NULL, L"Metadata space allocated", PhFormatSize(fileMetadataOptimization.MetadataSpaceAllocated, ULONG_MAX));
            PvAddChildLayoutNode(Context, NULL, L"Number of file records", PhFormatUInt64(fileMetadataOptimization.NumberOfFileRecords, TRUE));
            PvAddChildLayoutNode(Context, NULL, L"Number of resident attributes", PhFormatUInt64(fileMetadataOptimization.NumberOfResidentAttributes, TRUE));
            PvAddChildLayoutNode(Context, NULL, L"Number of nonresident attributes", PhFormatUInt64(fileMetadataOptimization.NumberOfNonresidentAttributes, TRUE));

            while (TRUE)
            {
                PPV_LAYOUT_NODE parentNode;

                parentNode = PvAddChildLayoutNode(Context, NULL, PvLayoutNameFlagsToString(fileLayoutNameEntry->Flags), PhCreateStringEx(fileLayoutNameEntry->FileName, fileLayoutNameEntry->FileNameLength));
                //PvAddChildLayoutNode(Context, parentNode, L"Parent Name", PvLayoutGetParentIdName(fileHandle, fileLayoutNameEntry->ParentFileReferenceNumber)->Buffer);
                PvAddChildLayoutNode(Context, parentNode, L"Parent ID", PhFormatString(L"%I64u (0x%I64x)", fileLayoutNameEntry->ParentFileReferenceNumber, fileLayoutNameEntry->ParentFileReferenceNumber));

                if (fileLayoutNameEntry->NextNameOffset == 0)
                    break;

                fileLayoutNameEntry = PTR_ADD_OFFSET(fileLayoutNameEntry, fileLayoutNameEntry->NextNameOffset);
            }

            while (TRUE)
            {
                PPV_LAYOUT_NODE parentNode;

                if (fileLayoutSteamEntry->Version != STREAM_LAYOUT_ENTRY_VERSION)
                {
                    status = STATUS_INVALID_KERNEL_INFO_VERSION;
                    break;
                }

                if (fileLayoutSteamEntry->AttributeTypeCode == 0x80)
                    parentNode = PvAddChildLayoutNode(Context, NULL, L"Stream", PhCreateString(L"::$DATA"));
                else
                    parentNode = PvAddChildLayoutNode(Context, NULL, L"Stream", PhCreateStringEx(fileLayoutSteamEntry->StreamIdentifier, fileLayoutSteamEntry->StreamIdentifierLength));

                PvAddChildLayoutNode(Context, parentNode, L"Attributes", PhFormatUInt64(fileLayoutSteamEntry->AttributeFlags, FALSE));
                PvAddChildLayoutNode(Context, parentNode, L"Attribute typecode", PhFormatString(L"0x%x", fileLayoutSteamEntry->AttributeTypeCode));
                PvAddChildLayoutNode(Context, parentNode, L"Flags", PvLayoutSteamFlagsToString(fileLayoutSteamEntry->Flags));
                PvAddChildLayoutNode(Context, parentNode, L"Size", PhFormatSize(fileLayoutSteamEntry->EndOfFile.QuadPart, ULONG_MAX));
                PvAddChildLayoutNode(Context, parentNode, L"Allocated Size", PhFormatSize(fileLayoutSteamEntry->AllocationSize.QuadPart, ULONG_MAX));

                if (fileLayoutSteamEntry->ExtentInformationOffset)
                {
                    PSTREAM_EXTENT_ENTRY streamExtentEntry;

                    streamExtentEntry = PTR_ADD_OFFSET(fileLayoutSteamEntry, fileLayoutSteamEntry->ExtentInformationOffset);
                    PvAddChildLayoutNode(Context, parentNode, L"Extents", PhFormatUInt64(streamExtentEntry->ExtentInformation.RetrievalPointers.ExtentCount, FALSE));
                }

                if (fileLayoutSteamEntry->NextStreamOffset == 0)
                    break;

                fileLayoutSteamEntry = PTR_ADD_OFFSET(fileLayoutSteamEntry, fileLayoutSteamEntry->NextStreamOffset);
            }
        }

        if (!NT_SUCCESS(status))
            break;

        if (input.Flags & QUERY_FILE_LAYOUT_RESTART)
        {
            input.Flags &= ~QUERY_FILE_LAYOUT_RESTART;
        }
    }

    if (status == STATUS_END_OF_FILE)
    {
        status = STATUS_SUCCESS;
    }

CleanupExit:

    if (volumeHandle)
    {
        NtClose(volumeHandle);
    }

    if (fileHandle)
    {
        NtClose(fileHandle);
    }

    PhDereferenceObject(volumeName);

    return status;
}

INT_PTR CALLBACK PvpPeLayoutDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPV_PROPPAGECONTEXT propPageContext;
    PPV_PE_LAYOUT_CONTEXT context;

    if (!PvPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext))
        return FALSE;

    if (uMsg == WM_INITDIALOG)
    {
        context = propPageContext->Context = PhAllocate(sizeof(PV_PE_LAYOUT_CONTEXT));
        memset(context, 0, sizeof(PV_PE_LAYOUT_CONTEXT));
    }
    else
    {
        context = propPageContext->Context;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            NTSTATUS status;

            context->WindowHandle = hwndDlg;
            context->TreeNewHandle = GetDlgItem(hwndDlg, IDC_SYMBOLTREE);
            PvInitializeLayoutTree(context);

            if (!NT_SUCCESS(status = PvLayoutEnumerateFileLayouts(context)))
            {
                PvLayoutSetStatusMessage(context, status);
            }

            TreeNew_NodesStructured(context->TreeNewHandle);

            PhInitializeWindowTheme(hwndDlg, PeEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PvDeleteLayoutTree(context);

            PhFree(context);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PvAddPropPageLayoutItem(hwndDlg, hwndDlg, PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PvAddPropPageLayoutItem(hwndDlg, context->TreeNewHandle, dialogItem, PH_ANCHOR_ALL);
                PvDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case WM_PV_LAYOUT_CONTEXTMENU:
                {
                    PPH_TREENEW_CONTEXT_MENU contextMenuEvent = (PPH_TREENEW_CONTEXT_MENU)lParam;
                    PPH_EMENU menu;
                    PPH_EMENU_ITEM selectedItem;
                    PPV_LAYOUT_NODE* nodes = NULL;
                    ULONG numberOfNodes = 0;

                    PvGetSelectedLayoutNodes(context, &nodes, &numberOfNodes);

                    if (numberOfNodes != 0)
                    {
                        menu = PhCreateEMenu();
                        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, USHRT_MAX, L"Copy", NULL, NULL), ULONG_MAX);
                        PhInsertCopyCellEMenuItem(menu, USHRT_MAX, context->TreeNewHandle, contextMenuEvent->Column);

                        selectedItem = PhShowEMenu(
                            menu,
                            hwndDlg,
                            PH_EMENU_SHOW_SEND_COMMAND | PH_EMENU_SHOW_LEFTRIGHT,
                            PH_ALIGN_LEFT | PH_ALIGN_TOP,
                            contextMenuEvent->Location.x,
                            contextMenuEvent->Location.y
                            );

                        if (selectedItem && selectedItem->Id != ULONG_MAX)
                        {
                            if (!PhHandleCopyCellEMenuItem(selectedItem))
                            {
                                switch (selectedItem->Id)
                                {
                                case USHRT_MAX:
                                    {
                                        PPH_STRING text;

                                        text = PhGetTreeNewText(context->TreeNewHandle, 0);
                                        PhSetClipboardString(context->TreeNewHandle, &text->sr);
                                        PhDereferenceObject(text);
                                    }
                                    break;
                                }
                            }
                        }

                        PhDestroyEMenu(menu);
                    }

                    PhFree(nodes);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}
