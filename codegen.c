#include "rvcc.h"

//
// 代码生成
//

// 记录栈深度
static int Depth;

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

// 计算给定节点的绝对地址
// 如果报错，说明节点不在内存中
static void genAddr(Node *Nd) {
    if (Nd->Kind == ND_VAR) {
        // 偏移量是相对于fp的
        println("  addi a0, fp, %d", Nd->Var->Offset);
        return;
    }

    error("not an lvalue");
}

// 根据变量的链表计算出偏移量
static void assignLVarOffsets(Function *Prog) {
    int Offset = 0;
    // 读取所有变量
    for (Obj *Var = Prog->Locals; Var; Var = Var->Next) {
        // 每个变量分配8字节
        Offset += 8;
        // 为每个变量赋一个偏移量，或者说是栈中地址
        Var->Offset = -Offset;
    }
    // 将栈对齐到16字节
    Prog->StackSize = alignTo(Offset, 16);
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
void genExpr(Node *Nd) {
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

    error("invalid expression");
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
            // 无条件跳转语句，跳转到.L.return段
            // j offset是 jal x0, offset的别名指令
            println("  j .L.return");
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
            printf("  beqz a0, .L.else.%d\n", C);
            // 生成符合条件后的语句
            genStmt(Nd->Then);
            // 执行完后跳转到if语句后面的语句
            printf("  j .L.end.%d\n", C);
            // else代码块，else可能为空，故输出标签
            printf(".L.else.%d:\n", C);
            // 生成不符合条件后的语句
            if (Nd->Els)
                genStmt(Nd->Els);
            // 结束if语句，继续执行后面的语句
            printf(".L.end.%d:\n", C);
            return;
        }
        // 生成for循环语句 
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
            genStmt(Nd->Init);
            // 输出循环头部标签
            printf(".L.begin.%d:\n", C);
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
            if (Nd -> Inc)
                // 生成循环递增语句
                genExpr(Nd->Inc);
            // 跳转到循环头部
            printf("  j .L.begin.%d\n", C);
            // 输出循环尾部标签
            printf(".L.end.%d:\n", C);
            return;
        }
        default:
            error("invalid statement");
    }

}

    // 栈布局
    //-------------------------------// sp
    //              fp
    //-------------------------------// fp = sp-8
    //             变量
    //-------------------------------// sp = sp-8-StackSize
    //           表达式计算
    //-------------------------------//


// 代码生成入口函数，包含代码块的基础信息
void codegen(Function * Prog) {
    // 为本地变量计算偏移量, 以及决定函数最终的栈大小
    assignLVarOffsets(Prog);
    println("  .globl main");
    println("main:");

    // Prologue, 前言
    // 将fp压入栈中，保存fp的值
    println("  addi sp, sp, -8");
    println("  sd fp, 0(sp)");
    // 将sp写入fp
    println("  mv fp, sp");
    // 偏移量为实际变量所用的栈大小
    println("  addi sp, sp, -%d", Prog->StackSize);

    // 生成语句链表的代码
    genStmt(Prog->Body);
    Assert(Depth == 0, "bad depth: %d", Depth);

    // Epilogue，后语
    // 输出return段标签
    println(".L.return:");
    // 将fp的值改写回sp
    println("  mv sp, fp");
    // 将最早fp保存的值弹栈，恢复fp。
    println("  ld fp, 0(sp)");
    println("  addi sp, sp, 8");

    // 返回
    println("  ret");
}