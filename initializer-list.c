//! 解析初始化列表
#include"rvcc.h"
#include"parse.h"

/*
//  int A[2][3] = { {1, 2, 3}, {4, 5, 6}};
//
//  initializer-list:
//
//                                    Init(TY_ARRAY,len=2)
//                     +---------------+----------------+
//                     ↓                                ↓
//                 children(TY_ARRAY,len=3)          children(TY_ARRAY,len=3)
//          +----------+----------+           +---------+---------+ 
//          ↓          ↓          ↓           ↓         ↓         ↓
//      children   children   children    children   children   children      ->  ALL TY_INT
//         ↓           ↓          ↓           ↓         ↓         ↓
//       EXPR=1      EXPR=2     EXPR=3      EXPR=4    EXPR=5    EXPR=6    

//  AST:
//                                                    ND_EXPR_STMT
//                                                         ↓
//                                                      ND_COMMA
//                                          +--------------+---------------+
//                                      ND_COMMA                        ND_ASSIGN (A[1][2]=6)
//                                    +------+------+                   +--------+--------+              
//                                    ↓             ↓                   ↓                 ↓
//                               ND_COMMA       ND_ASSIGN          ND_DEREF           Init->Expr, 6
//                            +------+------+    (A[1][1]=5)           |
//                            ↓             ↓                          ↓
//                       ND_COMMA       ND_ASSIGN                   ND_ADD
//                    +------+------+     (A[1][0]=4)         +--------+--------+    
//                    ↓             ↓                         ↓                 ↓
//                ND_COMMA       ND_ASSIGN                ND_DEREF          ND_MUL (newAdd)
//             +------+------+    (A[0][2]=3)                  |            +------+------+ 
//             ↓             ↓                                 ↓            ↓             ↓
//         ND_COMMA       ND_ASSIGN                         ND_ADD         2(IDX)      4(sizeof(int))
//      +------+------+    (A[0][1]=2)                +--------+--------+ 
//      ↓             ↓                               ↓                 ↓
//  ND_NULL_EXPR    ND_ASSIGN                      ND_VAR            ND_MUL
//                    (A[0][0]=1)                             +--------+--------+ 
//                                                            ↓                 ↓
//                                                        1(Idx)        12(sizeof(A[3]))
//
//  input = char x[2][3]={"ab","cd"};

//  initializer-list:
//
//                                    Init(TY_INT)
//                                         ↓
//                                      EXPR = 2


//  input = int a = 2;
//  initializer-list:
//
//                                    Init(TY_ARRAY,len=2)
//                     +---------------+----------------+
//                     ↓                                ↓
//                 children(TY_ARRAY,len=3)          children(TY_ARRAY,len=3)
//          +----------+----------+           +---------+---------+ 
//          ↓          ↓          ↓           ↓         ↓         ↓
//      children   children   children    children   children   children      ->  ALL TY_CHAR
//         ↓           ↓          ↓           ↓         ↓         ↓
//      EXPR='a'    EXPR='b'   EXPR=0      EXPR='c'  EXPR='d'    EXPR=0
*/


extern Node *assign(Token **Rest, Token *Tok);
static void _initializer(Token **Rest, Token *Tok, Initializer *Init);

// 跳过多余的元素
static Token *skipExcessElement(Token *Tok) {
    if (equal(Tok, "{")) {
        Tok = skipExcessElement(Tok->Next);
        return skip(Tok, "}");
    }

    // 解析并舍弃多余的元素
    assign(&Tok, Tok);
    return Tok;
}

// 新建初始化器. 这里只创建了初始化器的框架结构
static Initializer *newInitializer(Type *Ty, bool IsFlexible) {
    Initializer *Init = calloc(1, sizeof(Initializer));
    // 存储原始类型
    Init->Ty = Ty;

    // 处理数组类型
    if (Ty->Kind == TY_ARRAY) {
        // 判断是否需要调整数组元素数并且数组不完整
        if (IsFlexible && Ty->Size < 0) {
            // 设置初始化器为可调整的，之后进行完数组元素数的计算后，再构造初始化器
            Init->IsFlexible = true;
            return Init;
        }

        // 为数组的最外层的每个元素分配空间
        Init->Children = calloc(Ty->ArrayLen, sizeof(Initializer *));
        // 遍历解析数组最外层的每个元素
        for (int I = 0; I < Ty->ArrayLen; ++I)
            Init->Children[I] = newInitializer(Ty->Base, false);
    }

    // 处理结构体
    if (Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) {
        // 计算结构体成员的数量
        int Len = 0;
        for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
            ++Len;
        // 初始化器的子项
        Init->Children = calloc(Len, sizeof(Initializer *));

        // 遍历子项进行赋值
        for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
            // 判断结构体是否是灵活的，同时成员也是灵活的并且是最后一个
            // 在这里直接构造，避免对于灵活数组的解析
            if (IsFlexible && Ty->IsFlexible && !Mem->Next) {
                Initializer *Child = calloc(1, sizeof(Initializer));
                Child->Ty = Mem->Ty;
                Child->IsFlexible = true;
                Init->Children[Mem->Idx] = Child;
            } else {
                // 对非灵活子项进行赋值
                Init->Children[Mem->Idx] = newInitializer(Mem->Ty, false);
            }
        }

        return Init;
    }

    return Init;
}

// 计算数组初始化元素个数
static int countArrayInitElements(Token *Tok, Type *Ty) {
    Initializer *Dummy = newInitializer(Ty->Base, false);
    // 项数
    int I = 0;

    // 遍历所有匹配的项
    for (; !consumeEnd(&Tok, Tok); I++) {
        if (I > 0)
            Tok = skip(Tok, ",");
        _initializer(&Tok, Tok, Dummy);
    }
    return I;
}

// arrayInitializer1 = "{" initializer ("," initializer)* ","? "}"
static void arrayInitializer1(Token **Rest, Token *Tok, Initializer *Init) {
    Tok = skip(Tok, "{");

    // 如果数组是可调整的，那么就计算数组的元素数，然后进行初始化器的构造
    if (Init->IsFlexible) {
        int Len = countArrayInitElements(Tok, Init->Ty);
        // 在这里Ty也被重新构造为了数组
        *Init = *newInitializer(arrayOf(Init->Ty->Base, Len), false);
    }

    // 遍历数组
    for (int I = 0; !consumeEnd(Rest, Tok); I++) {
        if (I > 0)
            Tok = skip(Tok, ",");

        // 正常解析元素
        if (I < Init->Ty->ArrayLen)
            _initializer(&Tok, Tok, Init->Children[I]);
        // 跳过多余的元素
        else
            Tok = skipExcessElement(Tok);
    }
}

// arrayIntializer2 = initializer ("," initializer)* ","?
static void arrayInitializer2(Token **Rest, Token *Tok, Initializer *Init) {
    // 如果数组是可调整的，那么就计算数组的元素数，然后进行初始化器的构造
    if (Init->IsFlexible) {
        int Len = countArrayInitElements(Tok, Init->Ty);
        *Init = *newInitializer(arrayOf(Init->Ty->Base, Len), false);
    }

    // 遍历数组
    for (int I = 0; I < Init->Ty->ArrayLen && !isEnd(Tok); I++) {
        if (I > 0)
            Tok = skip(Tok, ",");
        _initializer(&Tok, Tok, Init->Children[I]);
    }
    *Rest = Tok;
}

// structInitializer1 = "{" initializer ("," initializer)* ","? }"
static void structInitializer1(Token **Rest, Token *Tok, Initializer *Init) {
    Tok = skip(Tok, "{");

    // 成员变量的链表
    Member *Mem = Init->Ty->Mems;

    while (!consumeEnd(Rest, Tok)) {
        // Mem未指向Init->Ty->Mems，则说明Mem进行过Next的操作，就不是第一个
        if (Mem != Init->Ty->Mems)
            Tok = skip(Tok, ",");

        if (Mem) {
            // 处理成员
            _initializer(&Tok, Tok, Init->Children[Mem->Idx]);
            Mem = Mem->Next;
        } else {
        // 处理多余的成员
            Tok = skipExcessElement(Tok);
        }
    }
}

// structIntializer2 = initializer ("," initializer)* ","?
static void structInitializer2(Token **Rest, Token *Tok, Initializer *Init) {
    bool First = true;

    // 遍历所有成员变量
    for (Member *Mem = Init->Ty->Mems; Mem && !isEnd(Tok); Mem = Mem->Next) {
        if (!First)
            Tok = skip(Tok, ",");
        First = false;
        _initializer(&Tok, Tok, Init->Children[Mem->Idx]);
    }
    *Rest = Tok;
}


// unionInitializer = "{" initializer "}"
static void unionInitializer(Token **Rest, Token *Tok, Initializer *Init) {
    if (equal(Tok, "{")) {
        // 存在括号的情况
        _initializer(&Tok, Tok->Next, Init->Children[0]);
        // ","?
        consume(&Tok, Tok, ",");
        *Rest = skip(Tok, "}");
    } else {
        // 不存在括号的情况
        _initializer(Rest, Tok, Init->Children[0]);
    }
}

// stringInitializer = stringLiteral
static void stringInitializer(Token **Rest, Token *Tok, Initializer *Init) {
    // 如果是可调整的，就构造一个包含数组的初始化器
    // 字符串字面量在词法解析部分已经增加了'\0'
    if (Init->IsFlexible)
        *Init = *newInitializer(arrayOf(Init->Ty->Base, Tok->Ty->ArrayLen), false);

    // 取数组和字符串的最短长度
    int Len = MIN(Init->Ty->ArrayLen, Tok->Ty->ArrayLen);
    // 遍历赋值
    for (int I = 0; I < Len; I++)
        Init->Children[I]->Expr = newNum(Tok->Str[I], Tok);
    *Rest = Tok->Next;
}

// 临时转换Buf类型对Val进行存储
static void writeBuf(char *Buf, uint64_t Val, int Sz) {
    if (Sz == 1)
        *Buf = Val;
    else if (Sz == 2)
        *(uint16_t *)Buf = Val;
    else if (Sz == 4)
        *(uint32_t *)Buf = Val;
    else if (Sz == 8)
        *(uint64_t *)Buf = Val;
    else
        error("unreachable");
}

// initializer = stringInitializer | arrayInitializer | structInitializer
//             | unionInitializer |assign
// stringInitializer = stringLiteral

// arrayInitializer = arrayInitializer1 | arrayInitializer2
// arrayInitializer1 = "{" initializer ("," initializer)* ","? "}"
// arrayIntializer2 = initializer ("," initializer)* ","?

// structInitializer = structInitializer1 | structInitializer2
// structInitializer1 = "{" initializer ("," initializer)* ","? "}"
// structInitializer2 = initializer ("," initializer)* ","?

// unionInitializer = "{" initializer "}"

// 这里往框架结构上面添加了叶子节点(assign语句)
static void _initializer(Token **Rest, Token *Tok, Initializer *Init) {
    // 字符串字面量的初始化
    if (Init->Ty->Kind == TY_ARRAY && Tok->Kind == TK_STR) {
        stringInitializer(Rest, Tok, Init);
        return;
    }

    // 数组的初始化
    if (Init->Ty->Kind == TY_ARRAY) {
        if (equal(Tok, "{"))
            // 存在括号的情况
            arrayInitializer1(Rest, Tok, Init);
        else
            // 不存在括号的情况
            arrayInitializer2(Rest, Tok, Init);
        return;
    }

    // 结构体的初始化
    if (Init->Ty->Kind == TY_STRUCT) {
        // 存在括号的情况
        if (equal(Tok, "{")) {
            structInitializer1(Rest, Tok, Init);
            return;
        }
        // 不存在括号的情况
        Node *Expr = assign(Rest, Tok);
        addType(Expr);
        if (Expr->Ty->Kind == TY_STRUCT) {
            Init->Expr = Expr;
            return;
        }
        structInitializer2(Rest, Tok, Init);
        return;
    }

    // 联合体的初始化
    if (Init->Ty->Kind == TY_UNION) {
        unionInitializer(Rest, Tok, Init);
        return;
    }

    // 处理标量外的大括号，例如：int x = {3};
    if (equal(Tok, "{")) {
        _initializer(&Tok, Tok->Next, Init);
        *Rest = skip(Tok, "}");
        return;
    }

    // assign
    // 为节点存储对应的表达式
    Init->Expr = assign(Rest, Tok);
}

// 初始化器
static Initializer *initializer(Token **Rest, Token *Tok, Type *Ty, Type **NewTy) {
    // 新建一个解析了类型的初始化器
    Initializer *Init = newInitializer(Ty, true);
    // 解析需要赋值到Init中
    _initializer(Rest, Tok, Init);

    // struct {char a, b[];} T;
    if ((Ty->Kind == TY_STRUCT || Ty->Kind == TY_UNION) && Ty->IsFlexible) {
        // 复制结构体类型
        Ty = copyStructType(Ty);

        Member *Mem = Ty->Mems;
        // 遍历到最后一个成员
        while (Mem->Next)
            Mem = Mem->Next;
        // 灵活数组类型替换为实际的数组类型
        Mem->Ty = Init->Children[Mem->Idx]->Ty;
        // 增加结构体的类型大小
        Ty->Size += Mem->Ty->Size;

        // 将新类型传回变量
        *NewTy = Ty;
        return Init;
    }

    // 将新类型传回变量
    *NewTy = Init->Ty;
    // trace("%d, %d", Ty->Size, (*NewTy)->Size);       // Ty->Size could be -1, but NewTy->size is known
    return Init;
}

// 指派初始化表达式
// 这里其实是在找出一个地址，来充当assign节点的LHS.
// 一个指派器负责给数组（如果是的话）中的一个元素赋值。
static Node *initDesigExpr(InitDesig *Desig, Token *Tok) {
    // 返回Desig中的变量
    if (Desig->Var)
        return newVarNode(Desig->Var, Tok);

    // 返回Desig中的成员变量
    if (Desig->Mem) {
        Node *Nd = newUnary(ND_MEMBER, initDesigExpr(Desig->Next, Tok), Tok);
        Nd->Mem = Desig->Mem;
        return Nd;
    }

    // 需要赋值的变量名
    // 递归到次外层Desig，有此时最外层有Desig->Var或者Desig->Mem
    // 然后逐层计算偏移量
    Node *LHS = initDesigExpr(Desig->Next, Tok);
    // 偏移量
    Node *RHS = newNum(Desig->Idx, Tok);
    // 返回偏移后的变量地址
    return newUnary(ND_DEREF, newAdd(LHS, RHS, Tok), Tok);
}

// 创建局部变量的初始化, 利用init这个初始化器给desig完成赋值
// 之前创建好的初始化器还不能直接使用，需要转换到AST中的相应节点
static Node *createLVarInit(Initializer *Init, Type *Ty, InitDesig *Desig, Token *Tok) {
    if (Ty->Kind == TY_ARRAY) {
        // 预备空表达式的情况, 左下角的那个
        Node *Nd = newNode(ND_NULL_EXPR, Tok);
        for (int I = 0; I < Ty->ArrayLen; I++) {
            // 这里next指向了上一级Desig的信息
            InitDesig Desig2 = {Desig, I};  // next = Desig, index = I, var = NULL
            // 局部变量进行初始化
            Node *RHS = createLVarInit(Init->Children[I], Ty->Base, &Desig2, Tok);
            // 构造一个形如：NULL_EXPR，EXPR1，EXPR2…的二叉树
            Nd = newBinary(ND_COMMA, Nd, RHS, Tok);
        }
        return Nd;
    }
    // 被其他结构体赋过值，则会存在Expr因而不解析
    if (Ty->Kind == TY_STRUCT && !Init->Expr) {
        // 构造结构体的初始化器结构
        Node *Nd = newNode(ND_NULL_EXPR, Tok);

        for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
            // Desig2存储了成员变量
            InitDesig Desig2 = {Desig, .Mem = Mem};
            Node *RHS = createLVarInit(Init->Children[Mem->Idx], Mem->Ty, &Desig2, Tok);
            Nd = newBinary(ND_COMMA, Nd, RHS, Tok);
        }
        return Nd;
    }

    if (Ty->Kind == TY_UNION) {
        // Desig2存储了成员变量
        InitDesig Desig2 = {Desig, .Mem = Ty->Mems};
        // 只处理第一个成员变量
        return createLVarInit(Init->Children[0], Ty->Mems->Ty, &Desig2, Tok);
    }

    // 如果需要作为右值的表达式为空，则设为空表达式
    if (!Init->Expr)
        return newNode(ND_NULL_EXPR, Tok);

    // 变量等可以直接赋值的左值
    Node *LHS = initDesigExpr(Desig, Tok);
    return newBinary(ND_ASSIGN, LHS, Init->Expr, Tok);
}

// 对全局变量的初始化器写入数据. buf is then filled to var -> initdata
static Relocation *writeGVarData(Relocation *Cur, Initializer *Init, Type *Ty,
                                char *Buf, int Offset) {
    // 处理数组
    if (Ty->Kind == TY_ARRAY) {
        int Sz = Ty->Base->Size;
        for (int I = 0; I < Ty->ArrayLen; I++)
            Cur = writeGVarData(Cur, Init->Children[I], Ty->Base, Buf, Offset + Sz * I);
        return Cur;
    }

    // 处理结构体
    if (Ty->Kind == TY_STRUCT) {
        for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
            Cur = writeGVarData(Cur, Init->Children[Mem->Idx], Mem->Ty, Buf,
                            Offset + Mem->Offset);
        return Cur;
    }

    // 处理联合体
    if (Ty->Kind == TY_UNION) {
        return writeGVarData(Cur, Init->Children[0], Ty->Mems->Ty, Buf, Offset);
    }

    // 处理单精度浮点数
    if (Ty->Kind == TY_FLOAT) {
        // 将缓冲区加上偏移量转换为float*后访问
        *(float *)(Buf + Offset) = evalDouble(Init->Expr);
        return Cur;
    }

    // 处理双精度浮点数
    if (Ty->Kind == TY_DOUBLE) {
        // 将缓冲区加上偏移量转换为double*后访问
        *(double *)(Buf + Offset) = evalDouble(Init->Expr);
        return Cur;
    }

    // 这里返回，则会使Buf值为0
    if (!Init->Expr)
        return Cur;

    // 预设使用到的 其他全局变量的名称
    // note: we cant deref *label, but in eval2 we convert the arg to be **
    // and then it becomes assinable(although label points to NULL, but &label not)
    char *Label = NULL;
    uint64_t Val = eval2(Init->Expr, &Label);

    // 如果不存在Label，说明可以直接计算常量表达式的值
    if (!Label) {
        writeBuf(Buf + Offset, Val, Ty->Size);
        return Cur;
    }

    // 存在Label，则表示使用了其他全局变量
    Relocation *Rel = calloc(1, sizeof(Relocation));
    Rel->Offset = Offset;
    Rel->Label = Label;
    Rel->Addend = Val;
    // 压入链表顶部
    Cur->Next = Rel;
    return Cur->Next;
}

// 全局变量在编译时需计算出初始化的值，然后写入.data段。
void GVarInitializer(Token **Rest, Token *Tok, Obj *Var) {
    // 获取到初始化器
    Initializer *Init = initializer(Rest, Tok, Var->Ty, &Var->Ty);
    // 新建一个重定向的链表
    Relocation Head = {};

    // 写入计算过后的数据
    char *Buf = calloc(1, Var->Ty->Size);
    writeGVarData(&Head, Init, Var->Ty, Buf, 0);
    // 全局变量的数据
    Var->InitData = Buf;
    Var->Rel = Head.Next;
}

// 局部变量初始化器
Node *LVarInitializer(Token **Rest, Token *Tok, Obj *Var) {
    // 获取初始化器，将值与数据结构一一对应
    // 这里的Tok指向的是 "=" 后面的那个token
    Initializer *Init = initializer(Rest, Tok, Var->Ty, &Var->Ty);
    // 指派初始化
    InitDesig Desig = {.Var = Var};

    // 我们首先为所有元素赋0，然后有指定值的再进行赋值
    Node *LHS = newNode(ND_MEMZERO, Tok);
    LHS->Var = Var;

    // 创建局部变量的初始化
    Node *RHS = createLVarInit(Init, Var->Ty, &Desig, Tok);
    // 左部为全部清零，右部为需要赋值的部分
    return newBinary(ND_COMMA, LHS, RHS, Tok);
}