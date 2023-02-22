#include"rvcc.h"

//
// 从parse.c中分离出来。专门用来创建节点
//

// 新建一个未完全初始化的节点. kind and token
Node *newNode(NodeKind Kind, Token *Tok) {
    Node *Nd = calloc(1, sizeof(Node));
    Nd->Kind = Kind;
    Nd->Tok = Tok;
    return Nd;
}

// 新建一个单叉树
Node *newUnary(NodeKind Kind, Node *Expr, Token *Tok) {
    Node *Nd = newNode(Kind, Tok);
    Nd->LHS = Expr;
    return Nd;
}

// 新建一个二叉树节点
Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS, Token *Tok) {
    Node *Nd = newNode(Kind, Tok);
    Nd->LHS = LHS;
    Nd->RHS = RHS;
    return Nd;
}

// 新建一个数字节点
Node *newNum(int Val, Token *Tok) {
    Node *Nd = newNode(ND_NUM, Tok);
    Nd->Val = Val;
    return Nd;
}

// 解析各种加法.
// 其实是newBinary的一种特殊包装。
// 专门用来处理加法。 而且还会根据左右节点的类型自动对此次加法做出适应
Node *newAdd(Node *LHS, Node *RHS, Token *Tok) {
    // 为左右部添加类型
    addType(LHS);
    addType(RHS);

    // num + num
    if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
        return newBinary(ND_ADD, LHS, RHS, Tok);

    // 不能解析 ptr + ptr
    // has base type, meaning that it's a pointer
    if (LHS->Ty->Base && RHS->Ty->Base){
        error("can not add up two pointers.");
    }

    // 将 num + ptr 转换为 ptr + num
    if (!LHS->Ty->Base && RHS->Ty->Base) {
        Node *Tmp = LHS;
        LHS = RHS;
        RHS = Tmp;
    }

    // ptr + num
    // 指针加法，ptr+1，这里的1不是1个字节，而是1个元素的空间，所以需要 ×8 操作
    RHS = newBinary(ND_MUL, RHS, newNum(8, Tok), Tok);
    return newBinary(ND_ADD, LHS, RHS, Tok);
}

// 解析各种减法. 与newAdd类似
Node *newSub(Node *LHS, Node *RHS, Token *Tok) {
    // 为左右部添加类型
    addType(LHS);
    addType(RHS);

    // num - num
    if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
    return newBinary(ND_SUB, LHS, RHS, Tok);

    // ptr - num
    if (LHS->Ty->Base && isInteger(RHS->Ty)) {
        RHS = newBinary(ND_MUL, RHS, newNum(8, Tok), Tok);
        addType(RHS);
        Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
        // 节点类型为指针
        Nd->Ty = LHS->Ty;
        return Nd;
    }

    // ptr - ptr，返回两指针间有多少元素
    if (LHS->Ty->Base && RHS->Ty->Base) {
        Node *Nd = newBinary(ND_SUB, LHS, RHS, Tok);
        Nd->Ty = TyInt;
        return newBinary(ND_DIV, Nd, newNum(8, Tok), Tok);
    }

    error("%s: invalid operands", strndup(Tok->Loc, Tok->Len));
    return NULL;
}

// 新变量
Node *newVarNode(Obj* Var, Token *Tok) {
    Node *Nd = newNode(ND_VAR, Tok);
    Nd->Var = Var;
    return Nd;
}