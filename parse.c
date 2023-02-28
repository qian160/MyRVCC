#include"rvcc.h"

//    input = "1+2; 3-4;"
//    add a field 'next' to ast-tree node (下一语句, expr_stmt). see parse()
//    (TOK)
//    head -> EXPR_STMT   -> EXPR_STMT    -> (other stmts...)
//                |               |   (looks like straight, but in fact lhs. note: a node of )
//                |               |   (type 'EXPR_STMT' is also unary. see function exprStmt())
//               '+'             '-'
//               / \             / \
//              /   \           /   \
//            1      2         3     4

//    input = "{1; {2;}; 3;}" :
//
//                  compoundStmt                           LHS = NULL <- ND_BLOCK  -> RHS = NULL
//                      |                                                   |
//      ----------------+------------------                                 ↓ Body
//      |               |                 |                  LHS = 1 <- ND_EXPR_STMT -> RHS = NULL  . note: expr_stmt is unary.
//  ND_EXPR_STMT   compoundStmt      ND_EXPR_STMT      ->                   |
//      |               |                 |                                 ↓ Next         
//      1          ND_EXPR_STMT           3                             ND_BLOCK -> Body = ND_EXPR_STMT, LHS = 2
//                      |                                                   |
//                      2                                                   ↓ Next
//                                                           LHS = 3 <- ND_EXPR_STMT -> RHS = NULL

/*
//    这里也隐含了这样一层信息：类型为ND_BLOCK的节点并没有lhs与rhs. (每次构造的时候调用的也是newNode这个不完全初始化的函数)。 
//    真正有用的信息其实保留在Body里。最后codegen的时候遇到块语句，不用管lhs与rhs，直接去他的body里面遍历生成语句就好
*/
//      
//                           declarator
//                               ↑
//                +--------------+-------------+
//                |                funcParams  |
//                |                +----------+|
//                |                |          ||
//        int     **      fn      (int a, int b)  { ... ... }         and the whole thing is a functionDefination
//         ↑               ↑      |            |  |         |
//      declspec         ident    +-----+------+  +----+----+
//                                      ↓              ↓
//                                 typeSuffix     compoundStmt


// declarator = "*"* ( "(" ident ")" | "(" declarator ")" | ident ) typeSuffix


//                                     declarator1(type1)
//                                         ↑
//              +--------------------------+-----------------------------------+
//              |                                                              |
//              |                                                              |
//        int   **    (     *(  * (     foo     )   [6]          )     [6]     )   [6][6][6]
//      declspec            |   |      ident    | typeSuffix3    | typeSuffix2     typeSuffix1
//                          |   |               |                |
//                          |   +-------+-------+                |
//                          |           ↓                        |
//                          |    declarator3(type3)              |
//                          +--------------+---------------------+
//                                         ↓
//                                  declarator2(type2)


// final goal: to recognize declarator, the biggest one
// note that inside our declarator, there are many other sub-declarations
// since the function declarator() returns a type, we can call it recursively and build new type upon old ones(base)

// final type  = declspec + declarator1 + typeSuffix1
// declarator1 = declarator2 + typeSuffix2
// declarator2 = declarator3 + typeSuffix3

//      input = add(1 + 4, 2) + 5;
//
//                      +
//                     / \
//                   /     \
//                 /         \
//            ND_FUNCALL      5
//                |             1               // ugly...
//                ↓ Args       /
//           ND_EXPR_STMT -> +
//                |            \
//                ↓ Next        4
//           ND_EXPR_STMT -> 2

// ↓↑

// note: a single number can match almost all the cases.
// 越往下优先级越高

// program = (functionDefination | global-variables)*
// functionDefinition = declspec declarator compoundStmt*
// declspec = ("int" | "char" | "long" | "short" | "void" | structDecl | unionDecl)+
// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// typeSuffix = ( funcParams  | "[" num "]"  typeSuffix)?
// funcParams =  "(" (param ("," param)*)? ")"
//      param = declspec declarator

// structDecl = structUnionDecl
// unionDecl = structUnionDecl
// structUnionDecl = ident? ("{" structMembers "}")?
// structMembers = (declspec declarator (","  declarator)* ";")*

// compoundStmt = "{" (stmt | declaration)* "}"

// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"

// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | compoundStmt        // recursion
//        | exprStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
// exprStmt = expr? ";"

// expr = assign ("," expr)?
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-" | "*" | "&") unary | postfix 
// postfix = primary ("[" expr "]" | "." ident)* | | "->" ident)*
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" unary
//         | ident funcArgs?
//         | str
//         | num

// FuncArgs = "(" (expr ("," expr)*)? ")"
// funcall = ident "(" (assign ("," assign)*)? ")"

static Token *function(Token *Tok);
static Node *declaration(Token **Rest, Token *Tok);
static Type *declspec(Token **Rest, Token *Tok);
static Type *structDecl(Token **Rest, Token *Tok);
static Type *unionDecl(Token **Rest, Token *Tok);
static Type *declarator(Token **Rest, Token *Tok, Type *Ty);
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty);
static Node *compoundStmt(Token **Rest, Token *Tok);
static Node *stmt(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
static Node *assign(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *postfix(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

// 在解析时，全部的变量实例都被累加到这个列表里。
static Obj *Locals;    // 局部变量
static Obj *Globals;   // 全局变量
// note: it is allowed to have an variable defined both in global
// and local on this occasion, we will use the local variable

// 所有的域的链表
static Scope *Scp = &(Scope){};

//
// helper functions
//

// --------- scope ----------

// 进入域
static void enterScope(void) {
    Scope *S = calloc(1, sizeof(Scope));
    // 后来的在链表头部
    // 类似于栈的结构，栈顶对应最近的域
    S->Next = Scp;
    Scp = S;
}

// 结束当前域
static void leaveScope(void) {
    Scp = Scp->Next;
}

// 将变量存入当前的域中
static VarScope *pushScope(char *Name, Obj *Var) {
    VarScope *S = calloc(1, sizeof(VarScope));
//    S->Name = Name;
    S->Var = Var;
    // 后来的在链表头部
    S->Next = Scp->Vars;
    Scp->Vars = S;
    return S;
}

static void pushTagScope(Token *Tok, Type *Ty, bool is_struct) {
    TagScope *S = calloc(1, sizeof(TagScope));
    S->Name = tokenName(Tok);
    S->Ty = Ty;
    if(is_struct){
        S->Next = Scp->structTags;
        Scp->structTags = S;
    }
    else{
        S->Next = Scp->unionTags;
        Scp->unionTags = S;
    }
}

// ---------- variables managements ----------

// 通过名称，查找一个变量.
static Obj *findVar(Token *Tok) {
    // 此处越先匹配的域，越深层
    // inner scope has access to outer's
    for (Scope *S = Scp; S; S = S->Next)
        // 遍历域内的所有变量
        for (VarScope *S2 = S->Vars; S2; S2 = S2->Next)
            if (equal(Tok, S2->Var->Name))
                return S2->Var;
    return NULL;
}

// 新建变量. default 'islocal' = 0. helper fnction of the 2 below
static Obj *newVar(char *Name, Type *Ty) {
    Obj *Var = calloc(1, sizeof(Obj));
    Var->Name = Name;
    Var->Ty = Ty;
    pushScope(Name, Var);
    return Var;
}

// 在链表中新增一个局部变量
static Obj *newLVar(char *Name, Type *Ty) {
    Obj *Var = newVar(Name, Ty);
    Var->IsLocal = true;
    // 将变量插入头部
    Var->Next = Locals;
    Locals = Var;
    return Var;
}

// 在链表中新增一个全局变量
static Obj *newGVar(char *Name, Type *Ty) {
    Obj *Var = newVar(Name, Ty);
    Var->Next = Globals;
    Globals = Var;
    return Var;
}

// 通过Token查找标签
static Type *findTag(Token *Tok, bool is_struct) {
    for (Scope *S = Scp; S; S = S->Next)
        for (TagScope *S2 = is_struct? S->structTags: S->unionTags; S2; S2 = S2->Next)
            if (equal(Tok, S2->Name))
                return S2->Ty;
        return NULL;
}

// 获取标识符
static char *getIdent(Token *Tok) {
    if (Tok->Kind != TK_IDENT)
        error("%s: expected an identifier", tokenName(Tok));
    return tokenName(Tok);
}

// 将形参添加到Locals. name, type
static void createParamLVars(Type *Param) {
    if (Param) {
        // 先将最底部的加入Locals中，之后的都逐个加入到顶部，保持顺序不变
        createParamLVars(Param->Next);
        // 添加到Locals中
        newLVar(getIdent(Param->Name), Param);
    }
}

// 新增唯一名称
static char *newUniqueName(void) {
    static int Id = 0;
    return format(".L..%d", Id++);
}

// 新增匿名全局变量
static Obj *newAnonGVar(Type *Ty) {
    return newGVar(newUniqueName(), Ty);
}

// 新增字符串字面量
static Obj *newStringLiteral(char *Str, Type *Ty) {
    Obj *Var = newAnonGVar(Ty);
    Var->InitData = Str;
    return Var;
}

// 判断是否为类型名
static bool isTypename(Token *Tok) 
{
    static char *types[] = {"char", "int", "struct", "union", "long", "short", "void"};
    for(int i = 0; i < sizeof(types) / sizeof(*types); i++){
        if(equal(Tok, types[i]))
            return true;
    }
    return false;
}

//
// 创建节点
//

// 新建一个未完全初始化的节点. kind and token
static Node *newNode(NodeKind Kind, Token *Tok) {
    Node *Nd = calloc(1, sizeof(Node));
    Nd->Kind = Kind;
    Nd->Tok = Tok;
    return Nd;
}

// 新建一个单叉树
static Node *newUnary(NodeKind Kind, Node *Expr, Token *Tok) {
    Node *Nd = newNode(Kind, Tok);
    Nd->LHS = Expr;
    return Nd;
}

// 新建一个二叉树节点
static Node *newBinary(NodeKind Kind, Node *LHS, Node *RHS, Token *Tok) {
    Node *Nd = newNode(Kind, Tok);
    Nd->LHS = LHS;
    Nd->RHS = RHS;
    return Nd;
}

// 新建一个数字节点
static Node *newNum(int64_t Val, Token *Tok) {
    Node *Nd = newNode(ND_NUM, Tok);
    Nd->Val = Val;
    return Nd;
}

// 解析各种加法.
// 其实是newBinary的一种特殊包装。
// 专门用来处理加法。 会根据左右节点的类型自动对此次加法做出适应
static Node *newAdd(Node *LHS, Node *RHS, Token *Tok) {
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
    // 指针加法，ptr+1，这里的1不是1个字节，而是1个元素的空间，所以需要 ×size 操作
    RHS = newBinary(ND_MUL, RHS, newNum(LHS->Ty->Base->Size, Tok), Tok);
    return newBinary(ND_ADD, LHS, RHS, Tok);
}

// 解析各种减法. 与newAdd类似
static Node *newSub(Node *LHS, Node *RHS, Token *Tok) {
    // 为左右部添加类型
    addType(LHS);
    addType(RHS);

    // num - num
    if (isInteger(LHS->Ty) && isInteger(RHS->Ty))
    return newBinary(ND_SUB, LHS, RHS, Tok);

    // ptr - num
    if (LHS->Ty->Base && isInteger(RHS->Ty)) {
        RHS = newBinary(ND_MUL, RHS, newNum(LHS->Ty->Base->Size, Tok), Tok);
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
        return newBinary(ND_DIV, Nd, newNum(LHS->Ty->Base->Size, Tok), Tok);
    }

    error("%s: invalid operands", strndup(Tok->Loc, Tok->Len));
    return NULL;
}

// 新变量
static Node *newVarNode(Obj* Var, Token *Tok) {
    Node *Nd = newNode(ND_VAR, Tok);
    Nd->Var = Var;
    return Nd;
}

// 构造全局变量
static Token *globalVariable(Token *Tok) {
    // int a;
    bool First = true;
    Type *Basety = declspec(&Tok, Tok);
    // keep searching until we meet a ";"
    while (!consume(&Tok, Tok, ";")) {
        if (!First)
        Tok = skip(Tok, ",");
        First = false;

        Type *Ty = declarator(&Tok, Tok, Basety);
        newGVar(getIdent(Ty->Name), Ty);
    }
    return Tok;
}



//
// 生成AST（抽象语法树），语法解析
//


// functionDefinition = declspec declarator compoundStmt*
static Token *function(Token *Tok) {
    Type *BaseTy = declspec(&Tok, Tok);
    Type *Ty = declarator(&Tok, Tok, BaseTy);

    // functions are also global variables
    Obj *Fn = newGVar(getIdent(Ty->Name), Ty);

    // no function body, just a defination
    if(equal(Tok, ";"))
        return Tok->Next;
    // 清空全局变量Locals
    Locals = (void*)0;
    enterScope();
    // 函数参数
    createParamLVars(Ty->Params);
    Fn->Params = Locals;

    // 函数体存储语句的AST，Locals存储变量
    Fn->Body = compoundStmt(&Tok, Tok);
    Fn->Locals = Locals;
    leaveScope();
    return Tok;
}


// declspec = ("int" | "char" | "long" | "short" | "void" | structDecl | unionDecl)+
// 声明的 基础类型. declaration specifiers
static Type *declspec(Token **Rest, Token *Tok) {

    // 类型的组合，被表示为例如：LONG+LONG=1<<9
    // 可知long int和int long是等价的。
    enum {
        VOID  = 1 << 0,
        CHAR  = 1 << 2,
        SHORT = 1 << 4,
        INT   = 1 << 6,
        LONG  = 1 << 8,
        OTHER = 1 << 10,
    };

    Type *Ty = TyInt;
    int Counter = 0; // 记录类型相加的数值

    // 遍历所有类型名的Tok
    while (isTypename(Tok)) {
        if (equal(Tok, "struct") || equal(Tok, "union")) {
            if (equal(Tok, "struct"))
                Ty = structDecl(&Tok, Tok->Next);
            else
                Ty = unionDecl(&Tok, Tok->Next);
            Counter += OTHER;
            continue;
        }

        // 对于出现的类型名加入Counter
        // 每一步的Counter都需要有合法值
        if (equal(Tok, "void"))
            Counter += VOID;
        else if (equal(Tok, "char"))
            Counter += CHAR;
        else if (equal(Tok, "short"))
            Counter += SHORT;
        else if (equal(Tok, "int"))
            Counter += INT;
        else if (equal(Tok, "long"))
            Counter += LONG;
        else
            error("unreachable");

        // 根据Counter值映射到对应的Type
        switch (Counter) {
            case VOID:
                Ty = TyVoid;
                break;
            case CHAR:
                Ty = TyChar;
                break;
            case SHORT:
            case SHORT + INT:
                Ty = TyShort;
                break;
            case INT:
                Ty = TyInt;
                break;
            case LONG:
            case LONG + INT:
                Ty = TyLong;
                break;
            default:
                errorTok(Tok, "invalid type");
        }

        Tok = Tok->Next;
    }

    *Rest = Tok;
    return Ty;
}

/*declarator：
    声明符，其实是介于declspec（声明的基础类型）与一直到声明结束这之间的所有东西("{" for fn and ";" for var)。
    与前面的declspec搭配就完整地定义了一个函数的签名。也可以用来定义变量*/
// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// int *** (a)[6] | int **(*(*(**a[6])))[6] | int **a[6]
// the 2nd case is a little difficult to handle with... and that's also where the recursion begins
// examples: ***var, fn(int x), a
// a further step on type parsing. also help to assign Ty->Name
static Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
    // "*"*
    // 构建所有的（多重）指针
    while (consume(&Tok, Tok, "*"))
        Ty = pointerTo(Ty);
    // "(" declarator ")", 嵌套类型声明符
    if (equal(Tok, "(")) {
        // 记录"("的位置
        Token *Start = Tok;
        Type Dummy = {};
        declarator(&Tok, Start->Next, &Dummy);
        Tok = skip(Tok, ")");
        // 获取到括号后面的类型后缀，Ty为解析完的类型，Rest指向分号
        Ty = typeSuffix(Rest, Tok, Ty);
        // Ty整体作为Base去构造，返回Type的值
        return declarator(&Tok, Start->Next, Ty);
    }

    // not an identifier, can't be declared
    if (Tok->Kind != TK_IDENT)
        errorTok(Tok, "%s: expect an identifier name", tokenName(Tok));

    // typeSuffix
    Ty = typeSuffix(Rest, Tok->Next, Ty);
    // ident
    // 变量名 或 函数名
    Ty->Name = Tok;
    return Ty;
}

// funcParams =  "(" (param ("," param)*)? ")"
// param = declspec declarator
static Type *funcParams(Token **Rest, Token *Tok, Type *Ty) {
        // skip "(" at the begining of fn
        Tok = Tok -> Next;
        // 存储形参的链表
        Type Head = {};
        Type *Cur = &Head;
        while (!equal(Tok, ")")) {
            // funcParams = param ("," param)*
            // param = declspec declarator
            if (Cur != &Head)
                Tok = skip(Tok, ",");
            Type *BaseTy = declspec(&Tok, Tok);
            Type *DeclarTy = declarator(&Tok, Tok, BaseTy);
            // 将类型复制到形参链表一份. why copy?
            // because we are operating on a same address in this loop,
            // if not copy, the latter type will just cover the previous's.
            // trace("%p", DeclarTy);
            Cur->Next = copyType(DeclarTy);
            Cur = Cur->Next;
        }

        // 封装一个函数节点
        Ty = funcType(Ty);
        // 传递形参
        Ty -> Params = Head.Next;
        // skip ")" at the end of function
        *Rest = Tok->Next;
        return Ty;
}

// typeSuffix = ( funcParams?  | "[" num "] typeSuffix")?
// if function, construct its formal parms. otherwise do nothing
// since we want to recursively construct its type, we need to pass the former type as an argument
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty) {
    // ("(" funcParams? ")")?
    if (equal(Tok, "("))
        return funcParams(Rest, Tok, Ty);
    // "[" num "] typeSuffix"
    if (equal(Tok, "[")) {
        int64_t Sz = getNumber(Tok->Next);  // array size
        // skip num and ]
        Tok = skip(Tok->Next->Next, "]");
        Ty = typeSuffix(Rest, Tok, Ty);
        return arrayOf(Ty, Sz);         // recursion 
    }

    // nothing special here, just return the original type
    *Rest = Tok;
    return Ty;
}

// structMembers = (declspec declarator (","  declarator)* ";")*
static void structMembers(Token **Rest, Token *Tok, Type *Ty) {
    Member Head = {};
    Member *Cur = &Head;
    // struct {int a; int b;} x
    while (!equal(Tok, "}")) {
        // declspec
        Type *BaseTy = declspec(&Tok, Tok);
        int First = true;

        while (!consume(&Tok, Tok, ";")) {
            if (!First)
                Tok = skip(Tok, ",");
            First = false;

            Member *Mem = calloc(1, sizeof(Member));
            // declarator
            Mem->Ty = declarator(&Tok, Tok, BaseTy);
            Mem->Name = Mem->Ty->Name;
            Cur = Cur->Next = Mem;
        }
    }

    *Rest = Tok;
    Ty->Mems = Head.Next;
}

// structDecl = "{" structMembers "}"
// specially, this function has 2 usages:
//  1. declare a struct variable
//      struct (tag)? {int a; ...} foo; foo.a = 10;
//  2. use a struct tag to type a variable
//      struct tag bar; bar.a = 1;

// structUnionDecl = ident? ("{" structMembers "}")?
static Type *structUnionDecl(Token **Rest, Token *Tok, bool is_struct) {
    // 读取标签
    Token *Tag = NULL;
    if (Tok->Kind == TK_IDENT) {
        Tag = Tok;
        Tok = Tok->Next;
    }

    if (Tag && !equal(Tok, "{")) {
        Type *Ty = findTag(Tag, is_struct);
        if (!Ty)
            errorTok(Tag, "unknown struct or union type");
        *Rest = Tok;
        return Ty;
    }

    // 构造一个结构体
    Type *Ty = calloc(1, sizeof(Type));
    Ty->Kind = TY_STRUCT;
    structMembers(Rest, Tok->Next, Ty);
    Ty->Align = 1;

    // 如果有名称就注册结构体类型
    if (Tag)
        pushTagScope(Tag, Ty, is_struct);

    *Rest = skip(*Rest, "}");
    return Ty;
}

// structDecl = structUnionDecl
static Type *structDecl(Token **Rest, Token *Tok) {
    Type *Ty = structUnionDecl(Rest, Tok, true);
    Ty->Kind = TY_STRUCT;

    // 计算结构体内成员的偏移量
    int Offset = 0;
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
        Offset = alignTo(Offset, Mem->Ty->Align);
        Mem->Offset = Offset;
        Offset += Mem->Ty->Size;
        // determining the whole struct's alignment, which
        // depends on the biggest elem
        if (Ty->Align < Mem->Ty->Align)
            Ty->Align = Mem->Ty->Align;
    }
    Ty->Size = alignTo(Offset, Ty->Align);

    return Ty;
}

// unionDecl = structUnionDecl
static Type *unionDecl(Token **Rest, Token *Tok) {
    Type *Ty = structUnionDecl(Rest, Tok, false);
    Ty->Kind = TY_UNION;

    // 联合体需要设置为最大的对齐量与大小，变量偏移量都默认为0
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next) {
        if (Ty->Align < Mem->Ty->Align)
            Ty->Align = Mem->Ty->Align;
        if (Ty->Size < Mem->Ty->Size)
            Ty->Size = Mem->Ty->Size;
    }
    // 将大小对齐
    Ty->Size = alignTo(Ty->Size, Ty->Align);
    return Ty;
}

// 获取结构体成员
static Member *getStructMember(Type *Ty, Token *Tok) {
    for (Member *Mem = Ty->Mems; Mem; Mem = Mem->Next)
        if (Mem->Name->Len == Tok->Len &&
            !strncmp(Mem->Name->Loc, Tok->Loc, Tok->Len))
            return Mem;
        errorTok(Tok, "no such member");
    return NULL;
}

// a.x 
//      ND_MEMBER -> x (Nd->Mem)
//       /   
//     a (ND_VAR)
// 构建结构体成员的节点. LHS = that struct variable
static Node *structRef(Node *LHS, Token *Tok) {
    addType(LHS);
    if (LHS->Ty->Kind != TY_STRUCT && LHS->Ty->Kind != TY_UNION)
        errorTok(LHS->Tok, "not a struct or union");

    Node *Nd = newUnary(ND_MEMBER, LHS, Tok);
    Nd->Mem = getStructMember(LHS->Ty, Tok);
    return Nd;
}


// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
//      declarator = "*"* ident typeSuffix
// add a variable to current scope, then create a node with kind ND_ASSIGN if possible
static Node *declaration(Token **Rest, Token *Tok) {
    // declspec
    // 声明的 基础类型
    Type *Basety = declspec(&Tok, Tok);

    Node Head = {};
    Node *Cur = &Head;
    // 对变量声明次数计数
    int I = 0;
    // (declarator ("=" expr)? ("," declarator ("=" expr)?)*)?
    while (!equal(Tok, ";")) {
        // 第1个变量不必匹配 ","
        if (I++ > 0)
            Tok = skip(Tok, ",");

        // declarator
        // 声明获取到变量类型，包括变量名
        Type *Ty = declarator(&Tok, Tok, Basety);

        if(Ty->Kind == TY_VOID)
            errorTok(Tok, "variable declared void");

        Obj *Var = newLVar(getIdent(Ty->Name), Ty);
        // trace(" new local var: %s, type = %d", tokenName(Ty->Name), Ty->Kind)

        // 如果不存在"="则为变量声明，不需要生成节点，已经存储在Locals中了
        if (!equal(Tok, "="))
            continue;

        // 解析“=”后面的Token
        Node *LHS = newVarNode(Var, Ty->Name);
        // 解析递归赋值语句
        // we use assign instead of expr here to avoid expr's 
        // further parsing in ND_COMMA, which is not what we want
        Node *RHS = assign(&Tok, Tok->Next);
        Node *Node = newBinary(ND_ASSIGN, LHS, RHS, Tok);
        // 存放在表达式语句中
        Cur->Next = newUnary(ND_EXPR_STMT, Node, Tok);
        Cur = Cur->Next;
    }

    // 将所有表达式语句，存放在代码块中
    Node *Nd = newNode(ND_BLOCK, Tok);
    Nd->Body = Head.Next;
    *Rest = Tok->Next;
    return Nd;
}

// 解析复合语句
// compoundStmt = "{" (stmt | declaration)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
    // 这里使用了和词法分析类似的单向链表结构
    Tok = skip(Tok, "{");
    Node Head = {};
    Node *Cur = &Head;
    enterScope();
    // (stmt | declaration)* "}"
    while (!equal(Tok, "}")) {
        if (isTypename(Tok))
            Cur->Next = declaration(&Tok, Tok);
        // stmt
        else
            Cur->Next = stmt(&Tok, Tok);
        Cur = Cur->Next;
        addType(Cur);
    }
    leaveScope();
    // Nd的Body存储了{}内解析的语句
    Node *Nd = newNode(ND_BLOCK, Tok);
    Nd->Body = Head.Next;
    *Rest = Tok->Next;
    return Nd;
}

// 解析语句
// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | compoundStmt
//        | exprStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
static Node *stmt(Token **Rest, Token *Tok) { 
    // "return" expr ";"
    if (equal(Tok, "return")) {
        Node *Nd = newNode(ND_RETURN, Tok);
        Nd -> LHS = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ";");
        return Nd;
    }

    // 解析if语句
    // "if" "(" expr ")" stmt ("else" stmt)?
    if (equal(Tok, "if")) {
        Node *Nd = newNode(ND_IF, Tok);
        // "(" expr ")"，条件内语句
        Tok = skip(Tok->Next, "(");
        Nd->Cond = expr(&Tok, Tok);
        Tok = skip(Tok, ")");
        // stmt，符合条件后的语句
        Nd->Then = stmt(&Tok, Tok);
        // ("else" stmt)?，不符合条件后的语句
        if (equal(Tok, "else"))
            Nd->Els = stmt(&Tok, Tok->Next);
        *Rest = Tok;
        return Nd;
    }

    // "for" "(" exprStmt expr? ";" expr? ")" stmt
    if (equal(Tok, "for")) {
        Node *Nd = newNode(ND_FOR, Tok);
        // "("
        Tok = skip(Tok->Next, "(");

        // exprStmt
        Nd->Init = exprStmt(&Tok, Tok);

        // expr?
        if (!equal(Tok, ";"))
            Nd->Cond = expr(&Tok, Tok);
        // ";"
        Tok = skip(Tok, ";");

        // expr?
        if (!equal(Tok, ")"))
            Nd->Inc = expr(&Tok, Tok);
        // ")"
        Tok = skip(Tok, ")");

        // stmt
        Nd->Then = stmt(Rest, Tok);
        return Nd;
    }

    // "while" "(" expr ")" stmt
    // while(cond){then...}
    if (equal(Tok, "while")) {
        Node *Nd = newNode(ND_FOR, Tok);
        // "("
        Tok = skip(Tok->Next, "(");
        // expr
        Nd->Cond = expr(&Tok, Tok);
        // ")"
        Tok = skip(Tok, ")");
        // stmt
        Nd->Then = stmt(Rest, Tok);
        return Nd;
    }


    // compoundStmt
    if (equal(Tok, "{"))
//        return compoundStmt(Rest, Tok->Next);
        return compoundStmt(Rest, Tok);

    // exprStmt
    return exprStmt(Rest, Tok);
}

// 解析表达式语句
// exprStmt = expr? ";"
static Node *exprStmt(Token **Rest, Token *Tok) {
    // ";". empty statment
    // note: empty statement is marked as a block statement.
    // in genStmt(), a block statment will print all its inner nodes
    // which should be nothing here
    if (equal(Tok, ";")) {
        *Rest = Tok->Next;
        return newNode(ND_BLOCK, Tok);
    }
    // expr ";"
    Node *Nd = newNode(ND_EXPR_STMT, Tok);
    Nd -> LHS = expr(&Tok, Tok);
    *Rest = skip(Tok, ";");
    return Nd;
}

// 解析表达式
// expr = assign ("," expr)?
static Node *expr(Token **Rest, Token *Tok) {
    Node *Nd = assign(&Tok, Tok);

    if (equal(Tok, ","))
        // this is strange grammar...  the lhs will still make effects, and
        // the final value of the comma expr depends on its right-most one
        return newBinary(ND_COMMA, Nd, expr(Rest, Tok->Next), Tok);
    *Rest = Tok;
    return Nd;
}

// difference between expr and assign: expr will add a further step in parsing
// ND_COMMA, which could be unnecessary sometimes and causes bugs. use assign instead
// 解析赋值
// assign = equality ("=" assign)?. note: lvalue
static Node *assign(Token **Rest, Token *Tok) {
    // equality
    Node *Nd = equality(&Tok, Tok);

    // 可能存在递归赋值，如a=b=1
    // ("=" assign)?
    if (equal(Tok, "="))
        return Nd = newBinary(ND_ASSIGN, Nd, assign(Rest, Tok->Next), Tok);
    *Rest = Tok;
    return Nd;
}

// 解析相等性
// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **Rest, Token *Tok) {
    // relational
    Node *Nd = relational(&Tok, Tok);

    // ("==" relational | "!=" relational)*
    while (true) {
        Token * start = Tok;
        // "==" relational
        if (equal(Tok, "==")) {
            Nd = newBinary(ND_EQ, Nd, relational(&Tok, Tok->Next), start);
            continue;
        }

        // "!=" relational
        if (equal(Tok, "!=")) {
            Nd = newBinary(ND_NE, Nd, relational(&Tok, Tok->Next), start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析比较关系
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational(Token **Rest, Token *Tok) {
    // add
    Node *Nd = add(&Tok, Tok);

    // ("<" add | "<=" add | ">" add | ">=" add)*
    while (true) {
        Token *start = Tok;
        // "<" add
        if (equal(Tok, "<")) {
            Nd = newBinary(ND_LT, Nd, add(&Tok, Tok->Next), start);
            continue;
        }

        // "<=" add
        if (equal(Tok, "<=")) {
            Nd = newBinary(ND_LE, Nd, add(&Tok, Tok->Next), start);
            continue;
        }

        // ">" add
        // X>Y等价于Y<X
        if (equal(Tok, ">")) {
            Nd = newBinary(ND_LT, add(&Tok, Tok->Next), Nd, start);
            continue;
        }

        // ">=" add
        // X>=Y等价于Y<=X
        if (equal(Tok, ">=")) {
            Nd = newBinary(ND_LE, add(&Tok, Tok->Next), Nd, start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析加减
// add = mul ("+" mul | "-" mul)*
static Node *add(Token **Rest, Token *Tok) {
    // mul
    Node *Nd = mul(&Tok, Tok);

    // ("+" mul | "-" mul)*
    while (true) {
        Token * start = Tok;
        // "+" mul
        if (equal(Tok, "+")) {
            Nd = newAdd(Nd, mul(&Tok, Tok->Next), start);
            continue;
        }

        // "-" mul
        if (equal(Tok, "-")) {
            Nd = newSub(Nd, mul(&Tok, Tok->Next), start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析乘除
// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **Rest, Token *Tok) {
    // unary
    Node *Nd = unary(&Tok, Tok);

    // ("*" unary | "/" unary)*
    while (true) {
        Token * start = Tok;
        // "*" unary
        if (equal(Tok, "*")) {
            Nd = newBinary(ND_MUL, Nd, unary(&Tok, Tok->Next), start);
            continue;
        }

        // "/" unary
        if (equal(Tok, "/")) {
            Nd = newBinary(ND_DIV, Nd, unary(&Tok, Tok->Next), start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析一元运算
// unary = ("+" | "-" | "*" | "&") unary | postfix
static Node *unary(Token **Rest, Token *Tok) {
    // "+" unary
    if (equal(Tok, "+"))
        return unary(Rest, Tok->Next);
    // "-" unary
    if (equal(Tok, "-"))
        return newUnary(ND_NEG, unary(Rest, Tok->Next), Tok);
    // "*" unary. pointer
    if (equal(Tok, "*")) {
        return newUnary(ND_DEREF, unary(Rest, Tok->Next), Tok);
    }
    // "*" unary. pointer
    if (equal(Tok, "&")) {
        return newUnary(ND_ADDR, unary(Rest, Tok->Next), Tok);
    }
    // primary
    return postfix(Rest, Tok);
}

/*
//  essence: convert the [] operator to some pointer dereferrence
//    input = a[5][10]

//    primary = a

//                   Nd
//                   | deref
//                   +
//                 /   \
//                /     \
//               /       \
//              Nd    expr(idx=10)
//              | deref
//              +
//            /   \
//       primary  expr(idx=5)       */

// postfix = primary ("[" expr "]" | "." ident)* | "->" ident)*
static Node *postfix(Token **Rest, Token *Tok) {
    // primary
    Node *Nd = primary(&Tok, Tok);

    // ("[" expr "]")*
    while (true) {
        if (equal(Tok, "[")) {
            // x[y] 等价于 *(x+y)
            Token *Start = Tok;
            Node *Idx = expr(&Tok, Tok->Next);
            Tok = skip(Tok, "]");
            Nd = newUnary(ND_DEREF, newAdd(Nd, Idx, Start), Start);
            continue;
        }

        // "." ident
        if (equal(Tok, ".")) {
            Nd = structRef(Nd, Tok->Next);
            Tok = Tok->Next->Next;
            continue;
        }

        // "->" ident
        if (equal(Tok, "->")) {
            // x->y 等价于 (*x).y
            Nd = newUnary(ND_DEREF, Nd, Tok);
            Nd = structRef(Nd, Tok->Next);
            Tok = Tok->Next->Next;
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析函数调用. a helper function used by primary
// funcall = ident "(" (assign ("," assign)*)? ")"
// the arg `Tok` is an ident
static Node *funCall(Token **Rest, Token *Tok) {
    Token *Start = Tok;
    // get the 1st arg, or ")". jump skip indet and "("
    Tok = Tok->Next->Next;

    Node Head = {};
    Node *Cur = &Head;
    // expr ("," expr)*
    while (!equal(Tok, ")")) {
        if (Cur != &Head)
            Tok = skip(Tok, ",");
        // expr
        Cur->Next = assign(&Tok, Tok);
        Cur = Cur->Next;
    }

    *Rest = skip(Tok, ")");

    Node *Nd = newNode(ND_FUNCALL, Start);
    // ident
    Nd->FuncName = tokenName(Start);
    Nd->Args = Head.Next;
    return Nd;
}


// 解析括号、数字
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" unary
//         | ident funcArgs?
//         | str
//         | num
// FuncArgs = "(" (expr ("," expr)*)? ")"
static Node *primary(Token **Rest, Token *Tok) {
    // this needs to be parsed before "(" expr ")", otherwise the "(" will be consumed
    // "(" "{" stmt+ "}" ")"
    if (equal(Tok, "(") && equal(Tok->Next, "{")) {
        // This is a GNU statement expresssion.
        Node *Nd = newNode(ND_STMT_EXPR, Tok);
        Nd->Body = compoundStmt(&Tok, Tok->Next)->Body;
        *Rest = skip(Tok, ")");
        return Nd;
    }

    // "(" expr ")"
    if (equal(Tok, "(")) {
        Node *Nd = expr(&Tok, Tok->Next);
        *Rest = skip(Tok, ")");     // ?
        return Nd;
    }

    // "sizeof" unary
    if (equal(Tok, "sizeof")) {
        Node *Nd = unary(Rest, Tok->Next);
        addType(Nd);
        return newNum(Nd->Ty->Size, Tok);
    }


    // num
    if (Tok->Kind == TK_NUM) {
        Node *Nd = newNum(Tok->Val, Tok);
        *Rest = Tok->Next;
        return Nd;
    }

    // ident args?
    // args = "(" (expr ("," expr)*)? ")"
    if (Tok->Kind == TK_IDENT) {
        // 函数调用
        if (equal(Tok->Next, "("))
            return funCall(Rest, Tok);

        // ident
        Obj *Var = findVar(Tok);
        if (!Var)
            errorTok(Tok, "undefined variable");
        *Rest = Tok->Next;
        return newVarNode(Var, Tok);
    }

    // str, recognized in tokenize
    if (Tok->Kind == TK_STR) {
        Obj *Var = newStringLiteral(Tok->Str, arrayOf(TyChar, Tok->strLen));
        *Rest = Tok->Next;
        return newVarNode(Var, Tok);
    }
    errorTok(Tok, "expected an expression");
    return NULL;
}

// 语法解析入口函数
// program = (functionDefinition* | global-variable)*
Obj *parse(Token *Tok) {
    Globals = NULL;

    // fn or gv?
    // int fn()
    while (Tok->Kind != TK_EOF) {
        // current = int, next = ident, next -> next = (
        bool isFn = equal(Tok->Next->Next, "(");
        if (isFn)
            Tok = function(Tok);
        else
            Tok = globalVariable(Tok);
    }

    return Globals;
}