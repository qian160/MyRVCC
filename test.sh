#!/bin/bash
gcc=riscv64-linux-gnu-gcc
qemu=qemu-riscv64

COLOR_RED="\033[1;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NONE="\033[0m"

# 将下列代码编译为tmp2.o，"-xc"强制以c语言进行编译
cat <<EOF | $gcc -xc -c -o tmp2.o -
int ret3() { return 3; }
int ret5() { return 5; }
int add(int x, int y) { return x+y; }
int sub(int x, int y) { return x-y; }
int add6(int a, int b, int c, int d, int e, int f) {
  return a+b+c+d+e+f;
}
EOF

# 声明一个函数
# assert 期待值 输入值
assert() {
  expected="$1"
  input="$2"

  # 运行程序，传入期待值，将生成结果写入tmp.s汇编文件。
  # 如果运行不成功，则会执行exit退出。成功时会短路exit操作
  ./rvcc "$input" > tmp.s || exit
  # 编译rvcc产生的汇编文件
  $gcc -static -g -o tmp tmp.s tmp2.o

  # 运行生成出来目标文件
  # ./tmp
  $qemu ./tmp

  # 获取程序返回值，存入 实际值
  actual="$?"

  # 判断实际值，是否为预期值
  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    printf "$COLOR_RED FAIL! $COLOR_NONE\n"
    exit 1
  fi
}

# [1] 返回指定数值
#for ((i=0; i<=1000; i++)); do
#  assert $i $i
#done
# assert 期待值 输入值
# [1] 返回指定数值
assert 0 'int main() { return 0; }'
assert 42 'int main() { return 42; }'
printf "$COLOR_GREEN [1] $COLOR_NONE\n"

# [2] 支持+ -运算符
assert 34 'int main() { return 12-34+56; }'
printf "$COLOR_GREEN [2] $COLOR_NONE\n"

# [3] 支持空格
assert 41 'int main() { return  12 + 34 - 5 ; }'
printf "$COLOR_GREEN [3] $COLOR_NONE\n"

# [5] 支持* / ()运算符
assert 47 'int main() { return 5+6*7; }'
assert 15 'int main() { return 5*(9-6); }'
assert 17 'int main() { return 1-8/(2*2)+3*6; }'
printf "$COLOR_GREEN [5] $COLOR_NONE\n"

# [6] 支持一元运算的+ -
assert 10 'int main() { return -10+20; }'
assert 10 'int main() { return - -10; }'
assert 10 'int main() { return - - +10; }'
assert 48 'int main() { return ------12*+++++----++++++++++4; }'
printf "$COLOR_GREEN [6] $COLOR_NONE\n"

# [7] 支持条件运算符
assert 0 'int main() { return 0==1; }'
assert 1 'int main() { return 42==42; }'
assert 1 'int main() { return 0!=1; }'
assert 0 'int main() { return 42!=42; }'
assert 1 'int main() { return 0<1; }'
assert 0 'int main() { return 1<1; }'
assert 0 'int main() { return 2<1; }'
assert 1 'int main() { return 0<=1; }'
assert 1 'int main() { return 1<=1; }'
assert 0 'int main() { return 2<=1; }'
assert 1 'int main() { return 1>0; }'
assert 0 'int main() { return 1>1; }'
assert 0 'int main() { return 1>2; }'
assert 1 'int main() { return 1>=0; }'
assert 1 'int main() { return 1>=1; }'
assert 0 'int main() { return 1>=2; }'
assert 1 'int main() { return 5==2+3; }'
assert 0 'int main() { return 6==4+3; }'
assert 1 'int main() { return 0*9+5*2==4+4*(6/3)-2; }'
printf "$COLOR_GREEN [7] $COLOR_NONE\n"

# [9] 支持;分割语句
assert 3 'int main() { 1; 2;return 3; }'
assert 12 'int main() { 12+23;12+99/3;return 78-66; }'
printf "$COLOR_GREEN [9] $COLOR_NONE\n"

# [10] 支持单字母变量
assert 3 'int main() { int a=3;return a; }'
assert 8 'int main() { int a=3,z=5;return a+z; }'
assert 6 'int main() { int a,b; a=b=3;return a+b; }'
assert 5 'int main() { int a=3,b=4;a=1;return a+b; }'
printf "$COLOR_GREEN [10] $COLOR_NONE\n"

# [11] 支持多字母变量
assert 3 'int main() { int foo=3;return foo; }'
assert 74 'int main() { int foo2=70; int bar4=4;return foo2+bar4; }'
printf "$COLOR_GREEN [11] $COLOR_NONE\n"

# [12] 支持return
assert 1 'int main() { return 1; 2; 3; }'
assert 2 'int main() { 1; return 2; 3; }'
assert 3 'int main() { 1; 2; return 3; }'
printf "$COLOR_GREEN [12] $COLOR_NONE\n"

# [13] 支持{...}
assert 3 'int main() { {1; {2;} return 3;} }'
printf "$COLOR_GREEN [13] $COLOR_NONE\n"

# [14] 支持空语句
assert 5 'int main() { ;;; return 5; }'
printf "$COLOR_GREEN [14] $COLOR_NONE\n"

# [15] 支持if语句
assert 3 'int main() { if (0) return 2; return 3; }'
assert 3 'int main() { if (1-1) return 2; return 3; }'
assert 2 'int main() { if (1) return 2; return 3; }'
assert 2 'int main() { if (2-1) return 2; return 3; }'
assert 4 'int main() { if (0) { 1; 2; return 3; } else { return 4; } }'
assert 3 'int main() { if (1) { 1; 2; return 3; } else { return 4; } }'
printf "$COLOR_GREEN [15] $COLOR_NONE\n"

# [16] 支持for语句
assert 55 'int main() { int i=0; int j=0; for (i=0; i<=10; i=i+1) j=i+j; return j; }'
assert 3 'int main() { for (;;) {return 3;} return 5; }'
printf "$COLOR_GREEN [16] $COLOR_NONE\n"

# [17] 支持while语句
assert 10 'int main() { int i=0; while(i<10) { i=i+1; } return i; }'
printf "$COLOR_GREEN [17] $COLOR_NONE\n"

# [20] 支持一元& *运算符
assert 3 'int main() { int x=3; return *&x; }'
assert 3 'int main() { int x=3; int *y=&x; int **z=&y; return **z; }'
assert 5 'int main() { int x=3; int *y=&x; *y=5; return x; }'
printf "$COLOR_GREEN [20] $COLOR_NONE\n"

# [21] 支持指针的算术运算
assert 3 'int main() { int x=3; int y=5; return *(&y-1); }'
assert 5 'int main() { int x=3; int y=5; return *(&x+1); }'
assert 7 'int main() { int x=3; int y=5; *(&y-1)=7; return x; }'
assert 7 'int main() { int x=3; int y=5; *(&x+1)=7; return y; }'
printf "$COLOR_GREEN [21] $COLOR_NONE\n"

# [22] 支持int关键字
assert 8 'int main() { int x, y; x=3; y=5; return x+y; }'
assert 8 'int main() { int x=3, y=5; return x+y; }'
printf "$COLOR_GREEN [22] $COLOR_NONE\n"

# [23] 支持零参函数调用
assert 3 'int main() { return ret3(); }'
assert 5 'int main() { return ret5(); }'
assert 8 'int main() { return ret3()+ret5(); }'
printf "$COLOR_GREEN [23] $COLOR_NONE\n"

# [24] 支持最多6个参数的函数调用
assert 8 'int main() { return add(3, 5); }'
assert 2 'int main() { return sub(5, 3); }'
assert 21 'int main() { return add6(1,2,3,4,5,6); }'
assert 66 'int main() { return add6(1,2,add6(3,4,5,6,7,8),9,10,11); }'
assert 136 'int main() { return add6(1,2,add6(3,add6(4,5,6,7,8,9),10,11,12,13),14,15,16); }'
printf "$COLOR_GREEN [24] $COLOR_NONE\n"

# [25] 支持零参函数定义
assert 32 'int main() { return ret32(); } int ret32() { return 32; }'
printf "$COLOR_GREEN [25] $COLOR_NONE\n"

# [26] 支持最多6个参数的函数定义
assert 7 'int main() { return add2(3,4); } int add2(int x, int y) { return x+y; }'
assert 1 'int main() { return sub2(4,3); } int sub2(int x, int y) { return x-y; }'
assert 55 'int main() { return fib(9); } int fib(int x) { if (x<=1) return 1; return fib(x-1) + fib(x-2); }'
printf "$COLOR_GREEN [26] $COLOR_NONE\n"

# [27] 支持一维数组
assert 3 'int main() { int x[2]; int *y=&x; *y=3; return *x; }'
assert 3 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+2); }'
printf "$COLOR_GREEN [27] $COLOR_NONE\n"

# [28] 支持多维数组
assert 0 'int main() { int x[2][3]; int *y=x; *y=0; return **x; }'
assert 1 'int main() { int x[2][3]; int *y=x; *(y+1)=1; return *(*x+1); }'
assert 2 'int main() { int x[2][3]; int *y=x; *(y+2)=2; return *(*x+2); }'
assert 3 'int main() { int x[2][3]; int *y=x; *(y+3)=3; return **(x+1); }'
assert 4 'int main() { int x[2][3]; int *y=x; *(y+4)=4; return *(*(x+1)+1); }'
assert 5 'int main() { int x[2][3]; int *y=x; *(y+5)=5; return *(*(x+1)+2); }'
printf "$COLOR_GREEN [28] $COLOR_NONE\n"

# [29] 支持 [] 操作符
assert 3 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; 2[x]=5; return *(x+2); }'

assert 0 'int main() { int x[2][3]; int *y=x; y[0]=0; return x[0][0]; }'
assert 1 'int main() { int x[2][3]; int *y=x; y[1]=1; return x[0][1]; }'
assert 2 'int main() { int x[2][3]; int *y=x; y[2]=2; return x[0][2]; }'
assert 3 'int main() { int x[2][3]; int *y=x; y[3]=3; return x[1][0]; }'
assert 4 'int main() { int x[2][3]; int *y=x; y[4]=4; return x[1][1]; }'
assert 5 'int main() { int x[2][3]; int *y=x; y[5]=5; return x[1][2]; }'
printf "$COLOR_GREEN [29] $COLOR_NONE\n"

# [30] 支持 sizeof
assert 8 'int main() { int x; return sizeof(x); }'
assert 8 'int main() { int x; return sizeof x; }'
assert 8 'int main() { int *x; return sizeof(x); }'
assert 32 'int main() { int x[4]; return sizeof(x); }'
assert 96 'int main() { int x[3][4]; return sizeof(x); }'
assert 32 'int main() { int x[3][4]; return sizeof(*x); }'
assert 8 'int main() { int x[3][4]; return sizeof(**x); }'
assert 9 'int main() { int x[3][4]; return sizeof(**x) + 1; }'
assert 9 'int main() { int x[3][4]; return sizeof **x + 1; }'
assert 8 'int main() { int x[3][4]; return sizeof(**x + 1); }'
assert 8 'int main() { int x=1; return sizeof(x=2); }'
assert 1 'int main() { int x=1; sizeof(x=2); return x; }'
printf "$COLOR_GREEN [30] $COLOR_NONE\n"


# 如果运行正常未提前退出，程序将显示OK
printf "$COLOR_GREEN PASS! $COLOR_NONE\n"

