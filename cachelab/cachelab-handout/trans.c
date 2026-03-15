/* 
 * trans.c - Matrix transpose B = A^T
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    // 定义 12 个局部变量（实验允许的最大值）
    int i, j, k, l;
    int v1, v2, v3, v4, v5, v6, v7, v8;

    /* --- 场景 1: 32 x 32 矩阵 --- */
    if (M == 32 && N == 32) {
        for (i = 0; i < 32; i += 8) {
            for (j = 0; j < 32; j += 8) {
                for (k = i; k < i + 8; k++) {
                    // 一次性读取 A 的一行到寄存器（变量），避开对角线冲突
                    v1 = A[k][j]; v2 = A[k][j+1]; v3 = A[k][j+2]; v4 = A[k][j+3];
                    v5 = A[k][j+4]; v6 = A[k][j+5]; v7 = A[k][j+6]; v8 = A[k][j+7];
                    
                    B[j][k] = v1; B[j+1][k] = v2; B[j+2][k] = v3; B[j+3][k] = v4;
                    B[j+4][k] = v5; B[j+5][k] = v6; B[j+6][k] = v7; B[j+7][k] = v8;
                }
            }
        }
    }

    /* --- 场景 2: 64 x 64 矩阵 --- */
    else if (M == 64 && N == 64) {
        for (i = 0; i < 64; i += 8) {
            for (j = 0; j < 64; j += 8) {
                // 1. 处理 A 的上半部分 (前 4 行)
                for (k = i; k < i + 4; k++) {
                    v1 = A[k][j]; v2 = A[k][j+1]; v3 = A[k][j+2]; v4 = A[k][j+3];
                    v5 = A[k][j+4]; v6 = A[k][j+5]; v7 = A[k][j+6]; v8 = A[k][j+7];

                    B[j][k] = v1; B[j+1][k] = v2; B[j+2][k] = v3; B[j+3][k] = v4;
                    // 临时存放在 B 的右上角
                    B[j][k+4] = v5; B[j+1][k+4] = v6; B[j+2][k+4] = v7; B[j+3][k+4] = v8;
                }
                // 2. 处理 A 的下半部分 (后 4 行) 并进行 B 内部交换
                for (k = j; k < j + 4; k++) {
                    // A 的左下
                    v1 = A[i+4][k]; v2 = A[i+5][k]; v3 = A[i+6][k]; v4 = A[i+7][k];
                    // B 的右上临时值
                    v5 = B[k][i+4]; v6 = B[k][i+5]; v7 = B[k][i+6]; v8 = B[k][i+7];

                    B[k][i+4] = v1; B[k][i+5] = v2; B[k][i+6] = v3; B[k][i+7] = v4;
                    B[k+4][i] = v5; B[k+4][i+1] = v6; B[k+4][i+2] = v7; B[k+4][i+3] = v8;
                }
                // 3. 处理 A 的右下角
                for (k = i + 4; k < i + 8; k++) {
                    v1 = A[k][j+4]; v2 = A[k][j+5]; v3 = A[k][j+6]; v4 = A[k][j+7];
                    B[j+4][k] = v1; B[j+5][k] = v2; B[j+6][k] = v3; B[j+7][k] = v4;
                }
            }
        }
    }

    /* --- 场景 3: 61 x 67 矩阵 --- */
    else {
        // 使用 16x16 分块，因为地址不规则，不需要复杂的寄存器技巧
        for (i = 0; i < N; i += 16) {
            for (j = 0; j < M; j += 16) {
                for (k = i; k < i + 16 && k < N; k++) {
                    for (l = j; l < j + 16 && l < M; l++) {
                        B[l][k] = A[k][l];
                    }
                }
            }
        }
    }
}

/* 
 * registerFunctions - 注册函数
 */
void registerFunctions()
{
    registerTransFunction(transpose_submit, transpose_submit_desc); 
}

/* 
 * is_transpose - 检查结果是否正确
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) return 0;
        }
    }
    return 1;
}