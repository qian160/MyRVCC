#include "rvcc.h"

//
// 代码生成
//

// 记录栈深度
static int Depth;

// 用于函数参数的寄存器们
static char *ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5"};

static Function *CurrentFn;

// 代码段计数
static int count(void) {
    static int I = 1;
    return I++;
}

// 对齐到Align的整数倍
// (0,Align]返回Align
static int alignTo(int N, int Align) {
    return (N + Align - 1) / Align * Align;
}

// 压栈，将结果临时压入栈中备用
// sp为栈指针，栈反向向下增长，64位下，8个字节为一个单位，所以sp-8
// 当前栈指针的地址就是sp，将a0的值压入栈
// 不使用寄存器存储的原因是因为需要存储的值的数量是变化的。
static void push(void) {
    println("  addi sp, sp, -8");
    println("  sd a0, 0(sp)");
    Depth++;
}

// 弹栈，将sp指向的地址的值，弹出到a1
static void pop(char *Reg) {
    println("  ld %s, 0(sp)", Reg);
    println("  addi sp, sp, 8");
    Depth--;
}
static void genExpr(Node *Nd);
// 计算给定节点的绝对地址, 并打印
// 如果报错，说明节点不在内存中
static void genAddr(Node *Nd) {
    switch (Nd->Kind){
        // 变量
        case ND_VAR:
            // 偏移量是相对于fp的
            println("  addi a0, fp, %d", Nd->Var->Offset);
            return;
        // 解引用*
        case ND_DEREF:
            genExpr(Nd -> LHS);
            return;
        default:
            error("%s: not an lvalue", strndup(Nd->Tok->Loc, Nd->Tok->Len));
            break;
    }
}

// 根据变量的链表计算出偏移量
// 其实是为每个变量分配地址
static void assignLVarOffsets(Function *Prog) {
    // 为每个函数计算其变量所用的栈空间
    for (Function *Fn = Prog; Fn; Fn = Fn->Next) {
        int Offset = 0;
        // 读取所有变量
        for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
            // 每个变量分配8字节
            Offset += 8;
            // 为每个变量赋一个偏移量，或者说是栈中地址
            Var->Offset = -Offset;
        }
        // 将栈对齐到16字节
        Fn->StackSize = alignTo(Offset, 16);
    }
}

// sementics: print the asm from an ast whose root node is `Nd`
// steps: for each node,
// 1. if it is a leaf node, then directly print the answer and return
// 2. otherwise:
//      get the value of its rhs sub-tree first, 
//      save that answer to the stack. 
//      then get the lhs sub-tree's value.
//      now we have both sub-tree's value, 
//      and how to deal with these two values depends on current root node
// 生成表达式
static void genExpr(Node *Nd) {
    // 生成各个根节点
    switch (Nd->Kind) {
        // 加载数字到a0, leaf node
        case ND_NUM:
            println("  li a0, %d", Nd->Val);
            return;
        // 对寄存器取反
        case ND_NEG:
            genExpr(Nd->LHS);
            // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
            println("  neg a0, a0");
            return;
        // 变量
        case ND_VAR:
            // 计算出变量的地址，然后存入a0
            genAddr(Nd);
            // 访问a0地址中存储的数据，存入到a0当中
            println("  ld a0, 0(a0)");
            return;
        // 赋值
        case ND_ASSIGN:
            // 左部是左值，保存值到的地址
            genAddr(Nd->LHS);
            push();
            // 右部是右值，为表达式的值
            genExpr(Nd->RHS);
            pop("a1");
            println("  sd a0, 0(a1)");
            return;
        // 解引用. *var
        case ND_DEREF:
            genExpr(Nd->LHS);
            println("  ld a0, 0(a0)");
            return;
        // 取地址 &var
        case ND_ADDR:
            genAddr(Nd->LHS);
            return;
        // 函数调用
        case ND_FUNCALL:{
            // 记录参数个数
            int NArgs = 0;
            // 计算所有参数的值，正向压栈
            for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
                genExpr(Arg);
                push();
                NArgs++;
            }
            // 反向弹栈，a0->参数1，a1->参数2……
            for (int i = NArgs - 1; i >= 0; i--)
                pop(ArgReg[i]);
            // 调用函数
            // the contents of the function is generated by test.sh, not by rvccl
            println("  call %s", Nd->FuncName);
            return;
        }
        default:
            break;
    }

    // 递归到最右节点
    genExpr(Nd->RHS);
    // 将结果 a0 压入栈
    // rhs sub-tree's answer
    push();
    // 递归到左节点
    genExpr(Nd->LHS);
    // 将结果弹栈到a1
    pop("a1");

    // a0: lhs value. a1: rhs value
    // 生成各个二叉树节点
    switch (Nd->Kind) {
        case ND_ADD: // + a0=a0+a1
            println("  add a0, a0, a1");
            return;
        case ND_SUB: // - a0=a0-a1
            println("  sub a0, a0, a1");
            return;
        case ND_MUL: // * a0=a0*a1
            println("  mul a0, a0, a1");
            return;
        case ND_DIV: // / a0=a0/a1
            println("  div a0, a0, a1");
            return;
        case ND_EQ:
            // if a0 == a1, then a0 ^ a1 should be 0
            println("  xor a0, a0, a1");
            println("  seqz a0, a0");
            return;
        case ND_NE: // a0 != a1
            // if a0 != a1, then a0 ^ a1 should not be 0
            println("  xor a0, a0, a1");
            println("  snez a0, a0");
            return;
        case ND_LE: // a0 <= a1
            // a0 <= a1 -> !(a0 > a1)
            // note: '!' here means 0 -> 1, 1-> 0. 
            // which is different from the 'neg' inst
            println("  slt a0, a1, a0");
            println("  xori a0, a0, 1");
            return;
        case ND_LT: // a0 < a1
            println("  slt a0, a0, a1");
            return;
        default:
            break;
    }

    error("%s: invalid expression", Nd -> Tok -> Loc);
}

// 生成语句
static void genStmt(Node *Nd) {
    switch (Nd->Kind){
        // 生成代码块，遍历代码块的语句链表
        case ND_BLOCK:
            for (Node *N = Nd->Body; N; N = N->Next)
                genStmt(N);
            return;
        case ND_EXPR_STMT:
            // node of type EXPR_STMT is unary
            genExpr(Nd->LHS);
            return;
        case ND_RETURN:
            genExpr(Nd->LHS);
            // 无条件跳转语句，跳转到.L.return.%s段
            // j offset是 jal x0, offset的别名指令
            println("  j .L.return.%s", CurrentFn->Name);
            return;
        // 生成if语句
        case ND_IF: {
            /*
            if (!cond)  // judged by beqz inst
                j .L.else.%d
                ... (then)
                ...
            .L.else.%d:
                ... (else)
                ...
            .L.end.%d:
            */
            // 代码段计数
            int C = count();
            // 生成条件内语句
            genExpr(Nd->Cond);
            // 判断结果是否为0，为0(false)则跳转到else标签
            println("  beqz a0, .L.else.%d", C);
            // 生成符合条件后的语句
            genStmt(Nd->Then);
            // 执行完后跳转到if语句后面的语句
            println("  j .L.end.%d", C);
            // else代码块，else可能为空，故输出标签
            println(".L.else.%d:", C);
            // 生成不符合条件后的语句
            if (Nd->Els)
                genStmt(Nd->Els);
            // 结束if语句，继续执行后面的语句
            println(".L.end.%d:", C);
            return;
        }
        // 生成for 或 "while" 循环语句 
        // "for" "(" exprStmt expr? ";" expr? ")" stmt
/*
            ... (init)                  // optional
            ... (cond)
    +-->.L.begin.%d:                    // loop begins
    |       ... (body)            
    |       ... (cond)            
    |      (beqz a0, .L.end.%d)---+     // loop condition. optional
    +------(j .L.begin.%d)        |  
        .L.end.%d:  <-------------+
        
    note: when entering the loop body for the `1st time`,
    we will insert an cond and branch.
    this works as the init cond check.
    if not satisfied, we will bot enter the loop body
        */
        case ND_FOR: {
            // 代码段计数
            int C = count();
            // 生成初始化语句
            if(Nd->Init){
                genStmt(Nd->Init);
            }
            // 输出循环头部标签
            println(".L.begin.%d:", C);
            // 处理循环条件语句
            if (Nd->Cond) {
                // 生成条件循环语句
                genExpr(Nd->Cond);
                // 判断结果是否为0，为0则跳转到结束部分
                printf("  beqz a0, .L.end.%d\n", C);
            }
            // 生成循环体语句
            genStmt(Nd->Then);
            // 处理循环递增语句
            if (Nd -> Inc){
                printf("\n# Inc语句%d\n", C);
                // 生成循环递增语句
                genExpr(Nd->Inc);
            }
            // 跳转到循环头部
            println("  j .L.begin.%d", C);
            // 输出循环尾部标签
            println(".L.end.%d:", C);
            return;
        }
        default:
            error("%s: invalid statement", Nd->Tok->Loc);
    }

}
    // 栈布局
    //-------------------------------// sp
    //              ra
    //-------------------------------// ra = sp-8
    //              fp
    //-------------------------------// fp = sp-16
    //             变量
    //-------------------------------// sp = sp-16-StackSize
    //           表达式计算
    //-------------------------------//


// 代码生成入口函数，包含代码块的基础信息
void codegen(Function * Prog) {
    // 为本地变量计算偏移量, 以及决定函数最终的栈大小
    assignLVarOffsets(Prog);
    // 为每个函数单独生成代码
    for (Function *Fn = Prog; Fn; Fn = Fn->Next) {
        printf("  .globl %s\n", Fn->Name);
        printf("# =====%s段开始===============\n", Fn->Name);
        printf("%s:\n", Fn->Name);
        CurrentFn = Fn;

        // Prologue, 前言
        // 将ra寄存器压栈,保存ra的值
        printf("  addi sp, sp, -16\n");
        printf("  sd ra, 8(sp)\n");
        // 将fp压入栈中，保存fp的值
        printf("  sd fp, 0(sp)\n");
        // 将sp写入fp
        printf("  mv fp, sp\n");

        // 偏移量为实际变量所用的栈大小
        printf("  addi sp, sp, -%d\n", Fn->StackSize);

        // 生成语句链表的代码
        printf("# =====%s段主体===============\n", Fn->Name);
        genStmt(Fn->Body);
        Assert(Depth == 0, "bad depth: %d", Depth);

        // Epilogue，后语
        // 输出return段标签
        printf("# =====%s段结束===============\n", Fn->Name);
        printf(".L.return.%s:\n", Fn->Name);
        // 将fp的值改写回sp
        printf("  mv sp, fp\n");
        // 将最早fp保存的值弹栈，恢复fp。
        printf("  ld fp, 0(sp)\n");
        // 将ra寄存器弹栈,恢复ra的值
        printf("  ld ra, 8(sp)\n");
        printf("  addi sp, sp, 16\n");
        // 返回
        printf("  ret\n");
    }
}