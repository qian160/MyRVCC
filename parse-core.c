#include"rvcc.h"
#include"parse.h"


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

/*
//         input = 
//         switch (val) {
//           case 1:
//             ...
//           case 2:
//             ...
//           default:
//             ...
//         }
//
//                          Then
//          CurrentSwitch    ->     ND_BLOCK
//           /   |  \                   |
//         val   | BrkLabel             ↓ Body      CaseNext
//               ↓                    ND_CASE       ->      ND_CASE ->  NULL
//          DefaultCase             /    |    \            /   |   \
//            /      \        LHS=STMT val=2  label      LHS  val=1  label
//      LHS=STMT     label
//
*/

// ↓↑

// note: a single number can match almost all the cases.
// 越往下优先级越高

// program = (functionDefination | global-variables)*
// functionDefinition = declspec declarator compoundStmt*
// declspec = ("int" | "char" | "long" | "short" | "void" | "_Bool"
//              | "typedef" | "static"
//              | structDecl | unionDecl | typedefName | enumSpecifier)+

// enumSpecifier = ident? "{" enumList? "}"
//                 | ident ("{" enumList? "}")?
// enumList = ident ("=" constExpr)? ("," ident ("=" constExpr)?)*

// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) typeSuffix
// typeSuffix = ( funcParams  | "[" constExpr? "]"  typeSuffix)?
// funcParams =  "(" (param ("," param)*)? ")"
//      param = declspec declarator

// structDecl = structUnionDecl
// unionDecl = structUnionDecl
// structUnionDecl = ident? ("{" structMembers "}")?
// structMembers = (declspec declarator (","  declarator)* ";")*

// compoundStmt = "{" ( typedef | stmt | declaration)* "}"

// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"

// stmt = "return" expr ";"
//        | "if" "(" expr ")" stmt ("else" stmt)?
//        | compoundStmt
//        | exprStmt
//        | "for" "(" exprStmt expr? ";" expr? ")" stmt
//        | "while" "(" expr ")" stmt
//        | "goto" ident ";"
//        | ident ":" stmt
//        | "break" ";" | "continue" ";"
//        | "switch" "(" expr ")" stmt
//        | "case" constExpr ":" stmt
//        | "default" ":" stmt

// exprStmt = expr? ";"
// expr = assign ("," expr)?
// assign = conditional (assignOp assign)?
// conditional = logOr ("?" expr ":" conditional)?
//      logOr = logAnd ("||" logAnd)*
//      logAnd = bitOr ("&&" bitOr)*
//      bitOr = bitXor ("|" bitXor)*
//      bitXor = bitAnd ("^" bitAnd)*
//      bitAnd = equality ("&" equality)*
//      assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="
// equality = relational ("==" relational | "!=" relational)*
// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
// shift = add ("<<" add | ">>" add)*
// add = mul ("+" mul | "-" mul)*
// mul = cast ("*" cast | "/" cast | "%" cast)*
// cast = "(" typeName ")" cast | unary
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | postfix 
//       | ("++" | "--") unary
// postfix = primary ("[" expr "]" | "." ident)* | | "->" ident | "++" | "--")*
// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" unary
//         | ident funcArgs?
//         | str
//         | num
//         | "sizeof" "(typeName)"

// typeName = declspec abstractDeclarator
// abstractDeclarator = "*"* ("(" abstractDeclarator ")")? typeSuffix

// FuncArgs = "(" (expr ("," expr)*)? ")"
// funcall = ident "(" (assign ("," assign)*)? ")"

static Token *function(Token *Tok, Type *BaseTy, VarAttr *Attr);
static Node *declaration(Token **Rest, Token *Tok, Type *BaseTy);
static Type *declspec(Token **Rest, Token *Tok, VarAttr *Attr);
static Type *enumSpecifier(Token **Rest, Token *Tok);
static Type *structDecl(Token **Rest, Token *Tok);
static Type *unionDecl(Token **Rest, Token *Tok);
/*  */ Type *declarator(Token **Rest, Token *Tok, Type *Ty);   // used in parse-util...
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty);
static Node *compoundStmt(Token **Rest, Token *Tok);
static Node *stmt(Token **Rest, Token *Tok);
static Node *exprStmt(Token **Rest, Token *Tok);
static Node *expr(Token **Rest, Token *Tok);
static Node *assign(Token **Rest, Token *Tok);
/*  */ Node *conditional(Token **Rest, Token *Tok);
static Node *logOr(Token **Rest, Token *Tok);
static Node *logAnd(Token **Rest, Token *Tok);
static Node *bitOr(Token **Rest, Token *Tok);
static Node *bitXor(Token **Rest, Token *Tok);
static Node *bitAnd(Token **Rest, Token *Tok);
static Node *equality(Token **Rest, Token *Tok);
static Node *relational(Token **Rest, Token *Tok);
static Node *shift(Token **Rest, Token *Tok);
static Node *add(Token **Rest, Token *Tok);
/*  */ Node *newAdd(Node *LHS, Node *RHS, Token *Tok);
/*  */ Node *newSub(Node *LHS, Node *RHS, Token *Tok);
static Node *cast(Token **Rest, Token *Tok);
static Type *typename(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *unary(Token **Rest, Token *Tok);
static Node *postfix(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

static Token *parseTypedef(Token *Tok, Type *BaseTy);
// 在解析时，全部的变量实例都被累加到这个列表里。

Obj *Locals;    // 局部变量
Obj *Globals;   // 全局变量
// note: it is allowed to have an variable defined both in global
// and local on this occasion, we will use the local variable

// 所有的域的链表
Scope *Scp = &(Scope){};

// 指向当前正在解析的函数
static Obj *CurrentFn;

// 当前函数内的goto和标签列表
Node *Gotos;
Node *Labels;

// 当前goto跳转的目标(break is implemented by goto)
static char *BrkLabel;
// 当前continue跳转的目标
static char *ContLabel;
// 如果我们正在解析switch语句，则指向表示switch的节点。 否则为空。
static Node *CurrentSwitch;
// 记录这些标签名、节点是为了在后续递归解析相关语句的时候能拿来给节点赋值。同时也可以防止stray现象


//
// 生成AST（抽象语法树），语法解析
//


// 解析类型别名. ends with ";"
static Token *parseTypedef(Token *Tok, Type *BaseTy) {
    bool First = true;
    while (!consume(&Tok, Tok, ";")) {
        if (!First)
            Tok = skip(Tok, ",");
        First = false;
        Type *Ty = declarator(&Tok, Tok, BaseTy);
        // 类型别名的变量名存入变量域中，并设置类型
        pushScope(getIdent(Ty->Name))->Typedef = Ty;
    }
    return Tok;
}


// functionDefinition = declspec declarator compoundStmt*
static Token *function(Token *Tok, Type *BaseTy, VarAttr *Attr) {
    Type *Ty = declarator(&Tok, Tok, BaseTy);

    // functions are also global variables
    Obj *Fn = newGVar(getIdent(Ty->Name), Ty);
    Fn->IsStatic = Attr->IsStatic;

    // no function body, just a defination
    if(equal(Tok, ";"))
        return Tok->Next;
    
    CurrentFn = Fn;
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
    // 处理goto和标签
    resolveGotoLabels();
    return Tok;
}


// declspec = ("int" | "char" | "long" | "short" | "void"  | "_Bool"
//              | "typedef" | "static"
//              | structDecl | unionDecl | typedefName | enumSpecifier)+
// 声明的 基础类型. declaration specifiers
static Type *declspec(Token **Rest, Token *Tok, VarAttr *Attr) {
    // 类型的组合，被表示为例如：LONG+LONG=1<<9
    // 可知long int和int long是等价的。
    enum {
        VOID  = 1 << 0,
        BOOL  = 1 << 2,
        CHAR  = 1 << 4,
        SHORT = 1 << 6,
        INT   = 1 << 8,
        LONG  = 1 << 10,
        OTHER = 1 << 12,
    };

    Type *Ty = TyInt;
    int Counter = 0; // 记录类型相加的数值
    // typedef int intt
    // 遍历所有类型名的Tok
    while (isTypename(Tok)) {
        // 处理typedef关键字
        if (equal(Tok, "typedef") || equal(Tok, "static")) {
            if (!Attr)
                errorTok(Tok, "storage class specifier is not allowed in this context");
            if (equal(Tok, "typedef"))
                Attr->IsTypedef = true;
            else
                Attr->IsStatic = true;

            // typedef不应与static一起使用
            if (Attr->IsTypedef && Attr->IsStatic)
                errorTok(Tok, "typedef and static may not be used together");

            Tok = Tok->Next;
            continue;
        }

        // 处理用户定义的类型
        Type *Ty2 = findTypedef(Tok);
        if (equal(Tok, "struct") || equal(Tok, "union") || equal(Tok, "enum") || Ty2) {
            if (Counter)
                break;

            if (equal(Tok, "struct")) {
                Ty = structDecl(&Tok, Tok->Next);
            }
            else if (equal(Tok, "union")) {
                Ty = unionDecl(&Tok, Tok->Next);
            }
            else if(equal(Tok, "enum")){
                Ty = enumSpecifier(&Tok, Tok->Next);
            }
            else {
                // 将类型设为类型别名指向的类型
                Ty = Ty2;
                Tok = Tok->Next;
            }
            Counter += OTHER;
            continue;
        }


        // 对于出现的类型名加入Counter
        // 每一步的Counter都需要有合法值
        if (equal(Tok, "void"))
            Counter += VOID;
        else if(equal(Tok, "_Bool"))
            Counter += BOOL;
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
            case BOOL:
                Ty = TyBool;
                break;;
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
            case LONG + LONG:
            case LONG + LONG + INT:
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
Type *declarator(Token **Rest, Token *Tok, Type *Ty) {
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
    // 变量名 或 函数名, or typedef name
    Ty->Name = Tok;
    return Ty;
}

// funcParams =  "(" (param ("," param)*)? ")"
// param = declspec declarator
static Type *funcParams(Token **Rest, Token *Tok, Type *Ty) {
        // skip "(" at the begining of fn
        Tok = skip(Tok, "(");
        // 存储形参的链表
        Type Head = {};
        Type *Cur = &Head;
        while (!equal(Tok, ")")) {
            // funcParams = param ("," param)*
            // param = declspec declarator
            if (Cur != &Head)
                Tok = skip(Tok, ",");
            Type *Ty2 = declspec(&Tok, Tok, NULL);
            Ty2 = declarator(&Tok, Tok, Ty2);
            if (Ty2 -> Kind == TY_ARRAY){
                Token *Name = Ty2 -> Name;
                // pointerTo will call calloc to create a new Type,
                // which will clear the name field, so we need to keep and 
                // reassign the name
                Ty2 = pointerTo(Ty2 -> Base);
                Ty2 -> Name = Name;
            }
            // 将类型复制到形参链表一份. why copy?
            // because we may need to modify(cast) the type in the future.
            // if not copy, then the original type(say TyInt) will also be changed
            // which is unacceptable. something like ownership here
            Cur->Next = copyType(Ty2);
            //Cur->Next = Ty2;
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

// typeSuffix = ( funcParams?  | "[" constExpr? "] typeSuffix")?
// if function, construct its formal parms. otherwise do nothing
// since we want to recursively construct its type, we need to pass the former type as an argument
static Type *typeSuffix(Token **Rest, Token *Tok, Type *Ty) {
    // ("(" funcParams? ")")?
    if (equal(Tok, "("))
        return funcParams(Rest, Tok, Ty);
    // "[" constExpr? "] typeSuffix"
    if (equal(Tok, "[")) {
        Tok = skip(Tok, "[");
        // 无数组维数的 "[]"
        // sizeof(int(*)[][10])
        if(equal(Tok, "]")){
            Ty = typeSuffix(Rest, Tok->Next, Ty);
            return arrayOf(Ty, -1);
        }
        // 有数组维数的情况
        else{
            int64_t Sz = constExpr(&Tok, Tok);  // array size
            Tok = skip(Tok, "]");
            Ty = typeSuffix(Rest, Tok, Ty);
            return arrayOf(Ty, Sz);         // recursion 
        }
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
        Type *BaseTy = declspec(&Tok, Tok, NULL);
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
static Type *structUnionDecl(Token **Rest, Token *Tok) {
    // 读取标签
    Token *Tag = NULL;
    if (Tok->Kind == TK_IDENT) {
        Tag = Tok;
        Tok = Tok->Next;
    }

    if (Tag && !equal(Tok, "{")) {
        *Rest = Tok;
        Type *Ty = findTag(Tag);
        if (Ty)
            return Ty;
        // 构造不完整结构体
        Ty = structType();
        Ty->Size = -1;
        pushTagScope(Tag, Ty);

        return Ty;
    }

    // 构造一个结构体
    Type *Ty = structType();
    structMembers(Rest, Tok->Next, Ty);
    Ty->Align = 1;

    *Rest = skip(*Rest, "}");
    // 如果是重复定义，就覆盖之前的定义。否则有名称就注册结构体类型
    if (Tag) {
        for (TagScope *S = Scp->Tags; S; S = S->Next) {
            if (equal(Tag, S->Name)) {
                *S->Ty = *Ty;
                // why not Ty?
                return S->Ty;
            }
        }
        pushTagScope(Tag, Ty);
    }

    return Ty;
}

// structDecl = structUnionDecl
static Type *structDecl(Token **Rest, Token *Tok) {
    Type *Ty = structUnionDecl(Rest, Tok);
    Ty->Kind = TY_STRUCT;

    // 不完整结构体
    if (Ty->Size < 0)
        return Ty;

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
    Type *Ty = structUnionDecl(Rest, Tok);
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

// 获取枚举类型信息
// enumSpecifier = ident? "{" enumList? "}"
//               | ident ("{" enumList? "}")?
// enumList      = ident ("=" constExpr)? ("," ident ("=" num)?)*
static Type *enumSpecifier(Token **Rest, Token *Tok) {
    Type *Ty = enumType();
    // 读取标签
    // ident?
    Token *Tag = NULL;
    if (Tok->Kind == TK_IDENT) {
        Tag = Tok;
        Tok = Tok->Next;
    }

    // 处理没有{}的情况
    if (Tag && !equal(Tok, "{")) {
        Type *Ty = findTag(Tag);
        if (!Ty)
            errorTok(Tag, "unknown enum type");
        if (Ty->Kind != TY_ENUM)
            errorTok(Tag, "not an enum tag");
        *Rest = Tok;
        return Ty;
    }

    // "{" enumList? "}"
    Tok = skip(Tok, "{");

    // enumList
    // 读取枚举列表
    int I = 0;   // 第几个枚举常量
    int Val = 0; // 枚举常量的值
    while (!equal(Tok, "}")) {
        if (I++ > 0)
            Tok = skip(Tok, ",");

        char *Name = getIdent(Tok);
        Tok = Tok->Next;

        // 判断是否存在赋值
        if (equal(Tok, "="))
            Val = constExpr(&Tok, Tok->Next);
        // 存入枚举常量
        VarScope *S = pushScope(Name);
        S->EnumTy = Ty;
        S->EnumVal = Val++;
    }

    *Rest = Tok->Next;

    if (Tag)
        pushTagScope(Tag, Ty);
    return Ty;
}

// declaration =
//    declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
//      declarator = "*"* ident typeSuffix
// add a variable to current scope, then create a node with kind ND_ASSIGN if possible
static Node *declaration(Token **Rest, Token *Tok, Type *BaseTy) {
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
        Type *Ty = declarator(&Tok, Tok, BaseTy);
        if (Ty->Size < 0)
            errorTok(Tok, "variable has incomplete type");

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
// compoundStmt = "{" ( typedef | stmt | declaration)* "}"
static Node *compoundStmt(Token **Rest, Token *Tok) {
    // 这里使用了和词法分析类似的单向链表结构
    Tok = skip(Tok, "{");
    Node Head = {};
    Node *Cur = &Head;
    enterScope();
    // (stmt | declaration)* "}"
    while (!equal(Tok, "}")) {
        if (isTypename(Tok) && !equal(Tok->Next, ":")) {
            VarAttr Attr = {};
            Type *BaseTy = declspec(&Tok, Tok, &Attr);
            // 解析typedef的语句
            if (Attr.IsTypedef) {
                Tok = parseTypedef(Tok, BaseTy);
                continue;
            }

            // 解析变量声明语句
            Cur->Next = declaration(&Tok, Tok, BaseTy);
        }

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
//        | "goto" ident ";"
//        | ident ":" stmt
//        | "break;" | "continue;"
//        | "switch" "(" expr ")" stmt
//        | "case" num ":" stmt
//        | "default" ":" stmt
static Node *stmt(Token **Rest, Token *Tok) { 
    // "return" expr ";"
    
    if (equal(Tok, "return")) {
        Node *Nd = newNode(ND_RETURN, Tok);
        Node *Exp = expr(&Tok, Tok->Next);
        addType(Exp);
        *Rest = skip(Tok, ";");
        //  处理返回值的类型转换
        Nd -> LHS = newCast(Exp, CurrentFn->Ty->ReturnTy);
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
        // 存储此前break和continue标签的名称
        char *Brk = BrkLabel;
        char *Cont = ContLabel;
        Node *Nd = newNode(ND_FOR, Tok);
        // 设置break标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();
        ContLabel = Nd->ContLabel = newUniqueName();

        // 进入for循环域
        enterScope();
        // "("
        Tok = skip(Tok->Next, "(");

        if (isTypename(Tok)) {
            // 初始化循环变量
            Type *BaseTy = declspec(&Tok, Tok, NULL);
            Nd->Init = declaration(&Tok, Tok, BaseTy);
        } else {
            // 初始化语句
            Nd->Init = exprStmt(&Tok, Tok);
        }

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
        // 恢复此前的break和continue标签
        BrkLabel = Brk;
        ContLabel = Cont;

        leaveScope();
        return Nd;
    }

    // "while" "(" expr ")" stmt
    // while(cond){then...}
    // note: while is implemented by for
    if (equal(Tok, "while")) {
        // 存储此前break和continue标签的名称
        char *Brk = BrkLabel;
        char *Cont = ContLabel;
        Node *Nd = newNode(ND_FOR, Tok);
        // 设置break标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();
        ContLabel = Nd->ContLabel = newUniqueName();
        // "("
        Tok = skip(Tok->Next, "(");
        // expr
        Nd->Cond = expr(&Tok, Tok);
        // ")"
        Tok = skip(Tok, ")");

        // stmt
        Nd->Then = stmt(Rest, Tok);
        // 恢复此前的break和continue标签
        BrkLabel = Brk;
        ContLabel = Cont;
        return Nd;
    }

    // "goto" ident ";"
    if (equal(Tok, "goto")) {
        Node *Nd = newNode(ND_GOTO, Tok);
        Nd->Label = getIdent(Tok->Next);
        // 将Nd同时存入Gotos，最后用于解析UniqueLabel
        Nd->GotoNext = Gotos;
        Gotos = Nd;
        *Rest = skip(Tok->Next->Next, ";");
        return Nd;
    }

    // "break" ";"
    if (equal(Tok, "break")) {
        if (!BrkLabel)
            errorTok(Tok, "stray break");
        // 跳转到break标签的位置
        Node *Nd = newNode(ND_GOTO, Tok);
        Nd->UniqueLabel = BrkLabel;
        *Rest = skip(Tok->Next, ";");
        return Nd;
    }

    // "continue" ";"
    if (equal(Tok, "continue")) {
        if (!ContLabel)
            errorTok(Tok, "stray continue");
        // 跳转到continue标签的位置
        Node *Nd = newNode(ND_GOTO, Tok);
        Nd->UniqueLabel = ContLabel;
        *Rest = skip(Tok->Next, ";");
        return Nd;
    }
    // "switch" "(" expr ")" stmt
        if (equal(Tok, "switch")) {
        // 记录此前的CurrentSwitch
        Node *Sw = CurrentSwitch;

        Node *Nd = newNode(ND_SWITCH, Tok);
        Tok = skip(Tok->Next, "(");
        Nd->Cond = expr(&Tok, Tok);
        Tok = skip(Tok, ")");

        // 设置当前的CurrentSwitch
        CurrentSwitch = Nd;

        // 存储此前break标签的名称
        char *Brk = BrkLabel;
        // 设置break标签的名称
        BrkLabel = Nd->BrkLabel = newUniqueName();

        // 进入解析各个case
        // stmt
        Nd->Then = stmt(Rest, Tok);

        // 恢复此前CurrentSwitch
        CurrentSwitch = Sw;
        // 恢复此前break标签的名称
        BrkLabel = Brk;
        return Nd;
    }

    // "case" num ":" stmt
    if (equal(Tok, "case")) {
        if (!CurrentSwitch)
            errorTok(Tok, "stray case");
        // case后面的数值
        int Val = constExpr(&Tok, Tok->Next);

        Node *Nd = newNode(ND_CASE, Tok);

        Tok = skip(Tok, ":");
        Nd->Label = newUniqueName();
        // case中的语句
        Nd->LHS = stmt(Rest, Tok);
        // case对应的数值
        Nd->Val = Val;
        // 将旧的CurrentSwitch链表的头部存入Nd的CaseNext
        // insert from head
        Nd->CaseNext = CurrentSwitch->CaseNext;
        // 将Nd存入CurrentSwitch的CaseNext
        CurrentSwitch->CaseNext = Nd;
        return Nd;
    }

    // "default" ":" stmt
    if (equal(Tok, "default")) {
        if (!CurrentSwitch)
            errorTok(Tok, "stray default");

        Node *Nd = newNode(ND_CASE, Tok);
        Tok = skip(Tok->Next, ":");
        Nd->Label = newUniqueName();
        Nd->LHS = stmt(Rest, Tok);
        // 存入CurrentSwitch->DefaultCase的默认标签
        CurrentSwitch->DefaultCase = Nd;
        return Nd;
    }



    // ident ":" stmt
    // labels
    if (Tok->Kind == TK_IDENT && equal(Tok->Next, ":")) {
        Node *Nd = newNode(ND_LABEL, Tok);
        Nd->Label = tokenName(Tok);
        Nd->UniqueLabel = newUniqueName();
        Nd->LHS = stmt(Rest, Tok->Next->Next);
        // 将Nd同时存入Labels，最后用于goto解析UniqueLabel
        Nd->GotoNext = Labels;
        Labels = Nd;
        return Nd;
    }


    // compoundStmt
    if (equal(Tok, "{"))
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

// 转换 A op= B为 TMP = &A, *TMP = *TMP op B
// let the result be reflected in A
static Node *toAssign(Node *Binary) {
    // A
    addType(Binary->LHS);
    // B
    addType(Binary->RHS);
    Token *Tok = Binary->Tok;

    // TMP
    Obj *Var = newLVar("", pointerTo(Binary->LHS->Ty));

    // TMP = &A
    Node *Expr1 = newBinary(
        ND_ASSIGN, 
        newVarNode(Var, Tok),
        newUnary(ND_ADDR, Binary->LHS, Tok), 
        Tok
    );

    // *TMP = *TMP op B
    Node *Expr2 = newBinary(
        ND_ASSIGN, 
        newUnary(ND_DEREF, newVarNode(Var, Tok), Tok),
        newBinary(Binary->Kind, newUnary(ND_DEREF, newVarNode(Var, Tok), Tok),
                Binary->RHS, Tok),
        Tok);

    // TMP = &A, *TMP = *TMP op B
    return newBinary(ND_COMMA, Expr1, Expr2, Tok);
}

// difference between expr and assign: expr will add a further step in parsing
// ND_COMMA, which could be unnecessary sometimes and causes bugs. use assign instead
// 解析赋值
// assign = conditional (assignOp assign)?
// assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="
static Node *assign(Token **Rest, Token *Tok) {
    // equality
    Node *Nd = conditional(&Tok, Tok);

    // 可能存在递归赋值，如a=b=1
    // ("=" assign)?
    if (equal(Tok, "="))
        return Nd = newBinary(ND_ASSIGN, Nd, assign(Rest, Tok->Next), Tok);
    // ("+=" assign)?
    if (equal(Tok, "+="))
        return toAssign(newAdd(Nd, assign(Rest, Tok->Next), Tok));
    // ("-=" assign)?
    if (equal(Tok, "-="))
        return toAssign(newSub(Nd, assign(Rest, Tok->Next), Tok));
    // ("*=" assign)?
    if (equal(Tok, "*="))
        return toAssign(newBinary(ND_MUL, Nd, assign(Rest, Tok->Next), Tok));
    // ("/=" assign)?
    if (equal(Tok, "/="))
        return toAssign(newBinary(ND_DIV, Nd, assign(Rest, Tok->Next), Tok));
    if (equal(Tok, "%="))
        return toAssign(newBinary(ND_MOD, Nd, assign(Rest, Tok->Next), Tok));
    // ("&=" assign)?
    if (equal(Tok, "&="))
        return toAssign(newBinary(ND_BITAND, Nd, assign(Rest, Tok->Next), Tok));

    // ("|=" assign)?
    if (equal(Tok, "|="))
        return toAssign(newBinary(ND_BITOR, Nd, assign(Rest, Tok->Next), Tok));

    // ("^=" assign)?
    if (equal(Tok, "^="))
        return toAssign(newBinary(ND_BITXOR, Nd, assign(Rest, Tok->Next), Tok));

    // ("<<=" assign)?
    if (equal(Tok, "<<="))
        return toAssign(newBinary(ND_SHL, Nd, assign(Rest, Tok->Next), Tok));
    // (">>=" assign)?
    if (equal(Tok, ">>="))
        return toAssign(newBinary(ND_SHR, Nd, assign(Rest, Tok->Next), Tok));


    *Rest = Tok;
    return Nd;
}

// 解析条件运算符
// conditional = logOr ("?" expr ":" conditional)?
Node *conditional(Token **Rest, Token *Tok) {
    // logOr
    Node *Cond = logOr(&Tok, Tok);

    // "?"
    if (!equal(Tok, "?")) {
        *Rest = Tok;
        return Cond;
    }

    // expr ":" conditional
    Node *Nd = newNode(ND_COND, Tok);
    Nd->Cond = Cond;
    // expr ":"
    Nd->Then = expr(&Tok, Tok->Next);
    Tok = skip(Tok, ":");
    // conditional，这里不能被解析为赋值式
    Nd->Els = conditional(Rest, Tok);
    return Nd;
}


// 按位或
// bitOr = bitXor ("|" bitXor)*
static Node *bitOr(Token **Rest, Token *Tok) {
    Node *Nd = bitXor(&Tok, Tok);
    while (equal(Tok, "|")) {
        Token *Start = Tok;
        Nd = newBinary(ND_BITOR, Nd, bitXor(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}

// 逻辑或
// logOr = logAnd ("||" logAnd)*
static Node *logOr(Token **Rest, Token *Tok) {
    Node *Nd = logAnd(&Tok, Tok);
    while (equal(Tok, "||")) {
        Token *Start = Tok;
        Nd = newBinary(ND_LOGOR, Nd, logAnd(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}

// 逻辑与
// logAnd = bitOr ("&&" bitOr)*
static Node *logAnd(Token **Rest, Token *Tok) {
    Node *Nd = bitOr(&Tok, Tok);
    while (equal(Tok, "&&")) {
        Token *Start = Tok;
        Nd = newBinary(ND_LOGAND, Nd, bitOr(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}


// 按位异或
// bitXor = bitAnd ("^" bitAnd)*
static Node *bitXor(Token **Rest, Token *Tok) {
    Node *Nd = bitAnd(&Tok, Tok);
    while (equal(Tok, "^")) {
        Token *Start = Tok;
        Nd = newBinary(ND_BITXOR, Nd, bitAnd(&Tok, Tok->Next), Start);
    }
    *Rest = Tok;
    return Nd;
}

// 按位与
// bitAnd = equality ("&" equality)*
static Node *bitAnd(Token **Rest, Token *Tok) {
    Node *Nd = equality(&Tok, Tok);
    while (equal(Tok, "&")) {
        Token *Start = Tok;
        Nd = newBinary(ND_BITAND, Nd, equality(&Tok, Tok->Next), Start);
    }
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
// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **Rest, Token *Tok) {
    // add
    Node *Nd = shift(&Tok, Tok);

    // ("<" add | "<=" add | ">" add | ">=" add)*
    while (true) {
        Token *start = Tok;
        // "<" shift
        if (equal(Tok, "<")) {
            Nd = newBinary(ND_LT, Nd, shift(&Tok, Tok->Next), start);
            continue;
        }

        // "<=" shift
        if (equal(Tok, "<=")) {
            Nd = newBinary(ND_LE, Nd, shift(&Tok, Tok->Next), start);
            continue;
        }

        // ">" shift
        // X>Y等价于Y<X
        if (equal(Tok, ">")) {
            Nd = newBinary(ND_LT, shift(&Tok, Tok->Next), Nd, start);
            continue;
        }

        // ">=" shift
        // X>=Y等价于Y<=X
        if (equal(Tok, ">=")) {
            Nd = newBinary(ND_LE, shift(&Tok, Tok->Next), Nd, start);
            continue;
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析位移
// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **Rest, Token *Tok) {
    // add
    Node *Nd = add(&Tok, Tok);

    while (true) {
        Token *Start = Tok;
        // "<<" add
        if (equal(Tok, "<<")) {
            Nd = newBinary(ND_SHL, Nd, add(&Tok, Tok->Next), Start);
            continue;
        }
        // ">>" add
        if (equal(Tok, ">>")) {
            Nd = newBinary(ND_SHR, Nd, add(&Tok, Tok->Next), Start);
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
// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **Rest, Token *Tok) {
    // unary
    Node *Nd = cast(&Tok, Tok);

    // ("*" cast | "/" cast)*
    while (true) {
        Token * start = Tok;
        // "*" cast
        if (equal(Tok, "*")) {
            Nd = newBinary(ND_MUL, Nd, cast(&Tok, Tok->Next), start);
            continue;
        }

        // "/" cast
        if (equal(Tok, "/")) {
            Nd = newBinary(ND_DIV, Nd, cast(&Tok, Tok->Next), start);
            continue;
        }

        if (equal(Tok, "%")) {
            Nd = newBinary(ND_MOD, Nd, cast(&Tok, Tok->Next), start);
        }

        *Rest = Tok;
        return Nd;
    }
}

// 解析类型转换
// cast = "(" typeName ")" cast | unary
static Node *cast(Token **Rest, Token *Tok) {
    // cast = "(" typeName ")" cast
    if (equal(Tok, "(") && isTypename(Tok->Next)) {
        Token *Start = Tok;
        Type *Ty = typename(&Tok, Tok->Next);
        Tok = skip(Tok, ")");
        // 解析嵌套的类型转换
        Node *Nd = newCast(cast(Rest, Tok), Ty);
        Nd->Tok = Start;
        return Nd;
    }
    // unary
    return unary(Rest, Tok);
}



// 解析一元运算
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | postfix 
//       | ("++" | "--") unary
static Node *unary(Token **Rest, Token *Tok) {
    // "+" cast
    if (equal(Tok, "+"))
        return cast(Rest, Tok->Next);
    // "-" cast
    if (equal(Tok, "-"))
        return newUnary(ND_NEG, cast(Rest, Tok->Next), Tok);
    // "*" cast. pointer
    if (equal(Tok, "*")) {
        return newUnary(ND_DEREF, cast(Rest, Tok->Next), Tok);
    }
    // "*" cast. pointer
    if (equal(Tok, "&")) {
        return newUnary(ND_ADDR, cast(Rest, Tok->Next), Tok);
    }
    if (equal(Tok, "!"))
        return newUnary(ND_NOT, cast(Rest, Tok->Next), Tok);
    if (equal(Tok, "~"))
        return newUnary(ND_BITNOT, cast(Rest, Tok->Next), Tok);
    // 转换 ++i 为 i+=1;
    if (equal(Tok, "++"))
        return toAssign(
            newAdd(unary(Rest, Tok->Next), newNum(1, Tok), Tok));

    // 转换 +-i 为 i-=1
    // "--" unary
    if (equal(Tok, "--"))
        return toAssign(
            newSub(unary(Rest, Tok->Next), newNum(1, Tok), Tok));

    // primary
    return postfix(Rest, Tok);
}

// 转换 A++ 为 `(typeof A)((A += 1) - 1)`
// Increase Decrease
static Node *newIncDec(Node *Nd, Token *Tok, int Addend) {
    addType(Nd);
    return newCast(
            newAdd(toAssign(newAdd(Nd, newNum(Addend, Tok), Tok)),
            newNum(-Addend, Tok), Tok),
    Nd->Ty);
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

// postfix = primary ("[" expr "]" | "." ident)* | "->" ident | "++" | "--")*
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

        if (equal(Tok, "++")) {
            Nd = newIncDec(Nd, Tok, 1);
            Tok = Tok->Next;
            continue;
        }

        if (equal(Tok, "--")) {
            Nd = newIncDec(Nd, Tok, -1);
            Tok = Tok->Next;
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

    // 查找函数名
    VarScope *S = findVar(Start);
    if (!S)
        errorTok(Start, "implicit declaration of a function");
    if (!S->Var || S->Var->Ty->Kind != TY_FUNC)
        errorTok(Start, "not a function");

    // 函数名的类型
    Type *Ty = S->Var->Ty;
    // 函数形参的类型
    Type *ParamTy = Ty->Params;

    Node Head = {};
    Node *Cur = &Head;
    // expr ("," expr)*
    while (!equal(Tok, ")")) {
        if (Cur != &Head)
            Tok = skip(Tok, ",");
        // expr
        Node *Arg = assign(&Tok, Tok);
        addType(Arg);

        if (ParamTy) {
            if (ParamTy->Kind == TY_STRUCT || ParamTy->Kind == TY_UNION)
                errorTok(Arg->Tok, "passing struct or union is not supported yet");
            // 将参数节点的类型进行转换
            Arg = newCast(Arg, ParamTy);
            // 前进到下一个形参类型
            ParamTy = ParamTy->Next;
        }
        // 对参数进行存储
        Cur->Next = Arg;

        Cur = Cur->Next;
        addType(Cur);
    }

    *Rest = skip(Tok, ")");

    Node *Nd = newNode(ND_FUNCALL, Start);
    // ident
    Nd->FuncName = tokenName(Start);
    Nd->Args = Head.Next;
    Nd->FuncType = Ty;
    Nd->Ty = Ty->ReturnTy;
    return Nd;
}

// abstractDeclarator = "*"* ("(" abstractDeclarator ")")? typeSuffix
// note: the ident is not needed, which is difference from declarator
static Type *abstractDeclarator(Token **Rest, Token *Tok, Type *Ty) {
    // "*"*
    while (equal(Tok, "*")) {
        Ty = pointerTo(Ty);
        Tok = Tok->Next;
    }

    // ("(" abstractDeclarator ")")?
    if (equal(Tok, "(")) {
        Token *Start = Tok;
        Type Dummy = {};
        // 使Tok前进到")"后面的位置
        abstractDeclarator(&Tok, Start->Next, &Dummy);
        Tok = skip(Tok, ")");
        // 获取到括号后面的类型后缀，Ty为解析完的类型，Rest指向分号
        Ty = typeSuffix(Rest, Tok, Ty);
        // 解析Ty整体作为Base去构造，返回Type的值
        return abstractDeclarator(&Tok, Start->Next, Ty);
    }

    // typeSuffix
    return typeSuffix(Rest, Tok, Ty);
}

// typeName = declspec abstractDeclarator
// 获取类型的相关信息
static Type *typename(Token **Rest, Token *Tok) {
    // declspec
    Type *Ty = declspec(&Tok, Tok, NULL);
    // abstractDeclarator
    return abstractDeclarator(Rest, Tok, Ty);
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

    // "sizeof" "(" typeName ")"
    // sizeof (int **(*[6])[6])[6][6]
    if (equal(Tok, "sizeof") && equal(Tok->Next, "(") && isTypename(Tok->Next->Next)) {
        Token *Start = Tok;
        Type *Ty = typename(&Tok, Tok->Next->Next);
        *Rest = skip(Tok, ")");
        return newNum(Ty->Size, Start);
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
        // 查找变量（或枚举常量）
        VarScope *S = findVar(Tok);
        // it could happen that the name exists, but var not.
        // that's because we also push typedef's name into scope(see parseTypedef)
        // and in that case we didn't allocate a var in the varscope
        // e.g: typedef int myint; myint = 1;  =>  undefined variable
        if (!S || (!S->Var && !S->EnumTy))
            errorTok(Tok, "undefined variable");

        *Rest = Tok->Next;
        // 是否为变量
        if (S->Var)
            return newVarNode(S->Var, Tok);
        // 否则为枚举常量
        else
            return newNum(S->EnumVal, Tok);

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
// program = ( typedef | functionDefinition* | global-variable)*
Obj *parse(Token *Tok) {
    Globals = NULL;

    // fn or gv?
    // int *** fn(){},  int**** a;
    while (Tok->Kind != TK_EOF) {
        VarAttr Attr = {};
        // at first I just use "VarAttr Attr;"
        // but then the struct's member got random init value...
        // use = {} to clear the member...
        Type *BaseTy = declspec(&Tok, Tok, &Attr);
        if(Attr.IsTypedef){
            Tok = parseTypedef(Tok, BaseTy);
            continue;
        }
        // straightforward approach
        Token *Start = Tok;
        while(!equal(Start, ";") && !equal(Start, "("))
            Start = Start->Next;
        bool isFn = equal(Start, "(");

        if (isFn)
            Tok = function(Tok, BaseTy, &Attr);
        else
            Tok = globalVariable(Tok, BaseTy);
    }

    return Globals;
}