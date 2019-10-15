#include <iostream>
#include <atomic>
#include <set>
#include <iterator>

#define SetMark(p) (p|Mark)
#define ClearMark(p) (p & ~Mark)
#define IsMarked(p) (p & Mark)
#define INVALID -1 //effectively ruling out negetive values or at-least -1. Need to find a better way.

using namespace std;

int Mark = 0x1;

enum TxStatus{
    Active,
    Committed,
    Aborted
};

enum OpType{
    op_insert,
    op_delete,
    op_find,
    op_update // as per section 3.2
};

struct Operation{
    OpType type;
    int key;
    int value; // as per section 3.2 // IN ALGORIHM 10 LINE 18
};

struct Desc{
    int size;
    std::atomic <TxStatus> status; // !!!ALERT!!! ADDING TO ACCOMODATE ALGORITHM 3 LINE 10
    Operation ops[];
};

struct NodeInfo{
    Desc* desc;
    int opid;
    int value; // !!!ALERT!!! ADDING TO ACCOMODATE ALGORITHM 10's REQUIREMENT
};

struct Node{
    std::atomic <NodeInfo*> info; // !!!ALERT!!! improvied CAS on line 20 of Algorithm 10
    int key;
    int value; //as per section 3.2
};

enum OpReturn{
    success,
    fail,
    retry
};

//ALgorithm 1 -> Algorithm 9

int IsNodePresent(Node* n, int key){
    return (n->key == key);
}


bool IsKeyPresent(NodeInfo* info, Desc* desc){
    OpType op = info->desc->ops[info->opid].type;
    TxStatus status = info->desc->status.load();
    switch (status)
    {
        case(Active):{
            if (info->desc == desc){
                return (op==op_update or op==op_find or op==op_insert);
            }
            else{
                return (op==op_update or op==op_find or op==op_delete);
            }
            break;
        }

        case (Committed):{
            return (op==op_update or op==op_find or op==op_insert);
            break;
        }

        case(Aborted):{
            return (op==op_update or op==op_find or op==op_delete); // !!!ALERT!!! Improvised, check page 16 
            break;
        }
    }
}

bool IsValuePresent (NodeInfo* info){
    Operation op = info->desc->ops[info->opid];
    if (op.type == op_update or op.type == op_find and op.value!= INVALID){
        return(false);
    }
    return(true);
}

//Algorithm 2 -> Algorithm 10

bool ExecuteOps(Desc* desc, int opid); // Declaration

int MapUpdateInfo(Node* n, NodeInfo* info, bool wantkey){  // !!!ALERT!!! improvised
    NodeInfo* oldinfo = n->info;
    if(IsMarked(int(oldinfo))){ //improvised
        DO_DELETE (n);
        return (retry);
    }

    if(oldinfo->desc!=info->desc){
        ExecuteOps(oldinfo->desc, oldinfo->opid+1);
    }
    else{
        if((oldinfo->opid + 1) == (info->opid)){ // !!!ALERT!!! improvised
            return (success);
        }
    }

    bool hashkey = IsKeyPresent(oldinfo, info->desc); // !!!ALERT!!! HOPE IT WORKS

    if ((!hashkey and wantkey) or (hashkey and !wantkey)){
        return (fail);
    }
    
    if (info->desc->status != Active){
        return (fail);
    }

    Operation op = info->desc->ops[info->opid];

    Operation oldOp = oldinfo->desc->ops[oldinfo->opid];

    if (op.type == op_update or op.type == op_find){
        if (oldOp.value != n->value && oldinfo->desc->status == Committed and IsValuePresent(oldinfo)){ //  improvised !!!ALERT!!!
            n->value = oldOp.value;
        }
    }

    if (n->info.compare_exchange_strong(oldinfo, info)){
        if (op.type == op_find){
            if (oldOp.type == op_update or (oldOp.type == op_find && oldOp.value != INVALID)){
                n->info.load()->value = oldOp.value; // !!!ALERT!!! improvised
                return (n->info.load()->value);
            }
            else{
                return (n->value);
            }
        }
        else{
            return (success);
        }    
    }
    else{
        return (retry);
    }
}

// ALGORITHM 3 -> ALGORITHM 5


bool ExecuteOps (Desc* desc, int value, int opid){
    bool ret = true;
    std::set <Node*> delnodes;
    while (desc->status == Active and ret and opid < desc -> size){
        Operation* op = &(desc->ops[opid]);

        if (op->type == op_find){
            ret = Find(op->key, desc, opid);
        }
        else if (op->type == op_insert){
            ret = Insert(op->key, desc, opid);
        }
        else if (op->type == op_delete){
            Node* del;
            ret = Delete(op->key, desc, opid, del);
            delnodes.insert(del);
        }
        opid = opid + 1;
    }

    if (ret==true){
        TxStatus a = Active; // TO ACCOMMODATE COMPARE_EXCHANGE_STRONG
        TxStatus c = Committed; // TO ACCOMMODATE COMPARE_EXCHANGE_STRONG
        if (desc->status.compare_exchange_strong(a, c)){
            MarkDelete(delnodes, desc);
        }
    }
    else{
        TxStatus a = Active; // TO ACCOMMODATE COMPARE_EXCHANGE_STRONG
        TxStatus b = Aborted; // TO ACCOMMODATE COMPARE_EXCHANGE_STRONG
        desc->status.compare_exchange_strong(a, b);
    }

}

bool ExecuteTransaction (Desc* desc){
    ExecuteOps(desc, 0);
    return(desc->status.load()==Committed);
}

// ALGORITHM 6

bool Insert(int key, Desc* desc, int opid){ // TO ACCOMODATE VALUE FOR NODE
    NodeInfo* info = new NodeInfo;
    info->desc = desc;
    info->opid = opid;
    int ret;

    while(true){
        Node* curr = DO_LOCATEPRED(key);
        if (IsNodePresent(curr, key)){
            ret = MapUpdateInfo(curr, info, false);
        }
        else{
            Node* n = new Node;
            n->key = key;
            n->info = info;
            n->value = desc->ops[opid].value; // ASSIGN VALUE FOR HASHMAP FROM DESCRIPTOR -> OPERATIONS ID -> VALUE
            ret = DO_INSERT(n);
        }

        if(ret==success){
            return(true);
        }
        else if(ret==fail){
            return(false);
        }
    }
}

// ALGORITHM 7

bool Delete (int key, Desc* desc, int opid, Node*& del){
    NodeInfo* info = new NodeInfo;
    info->desc = desc;
    info->opid = opid;
    int ret;
    while(true){
        Node* curr = DO_LOCATEPRED (key);
        if(IsNodePresent(curr, key)){
            ret = MapUpdateInfo(curr, info, true);
        }
        else{
            ret = fail;
        }

        if(ret==success){
            del = curr;
            return (true);
        }

        else if (ret==fail){
            del = NULL;
            return(false);
        }
    }
}

void MarkDelete (std::set<Node*> delnodes, Desc* desc){
    std::set<Node*>::iterator del;
    for (del = delnodes.begin(); del!=delnodes.end();  ++del){
        if(*del==NULL){
            continue;
        }
        Node* temp = *del; // TO ACCOMODATE THE QUIRKS OF STD::SET 
        NodeInfo* info = temp->info.load();
        if(info->desc != desc){
            continue;
        }
        if(temp->info.compare_exchange_strong(info, (NodeInfo*)SetMark(int(info)))){
            DO_DELETE (*del);
        }
    }
}

bool Find (int key, Desc* desc, int opid){
    NodeInfo* info = new NodeInfo;
    info->desc = desc;
    info->opid = opid;
    int ret;
    while(true){
        Node* curr = DO_LOCATEPRED(key);
        if (IsNodePresent(curr, key)){
            ret = MapUpdateInfo(curr, info, true);
        }
        else{
            ret = fail;
        }

        if(ret==success){
            return(true);
        }
        else if(ret==fail){
            return(false);
        }
    } 
}
