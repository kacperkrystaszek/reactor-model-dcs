#pragma once
#include <array>
#include <stdexcept>
#include <cmath>

template <size_t Rows, size_t Cols>
struct Matrix {
    std::array<std::array<float, Cols>, Rows> data{};

    Matrix() {
        for (size_t i = 0; i < Rows; ++i) {
            for (size_t j = 0; j < Cols; ++j) {
                data[i][j] = 0.0f;
            }
        }
    }

    static Matrix identity() {
        static_assert(Rows == Cols, "Identity matrix must be square");
        Matrix res;
        for (size_t i = 0; i < Rows; ++i) {
            res.data[i][i] = 1.0f;
        }
        return res;
    }

    Matrix<Cols, Rows> transpose() const {
        Matrix<Cols, Rows> res;
        for (size_t i = 0; i < Rows; ++i) {
            for (size_t j = 0; j < Cols; ++j) {
                res.data[j][i] = data[i][j];
            }
        }
        return res;
    }

    template <size_t OtherCols>
    Matrix<Rows, OtherCols> operator*(const Matrix<Cols, OtherCols>& other) const {
        Matrix<Rows, OtherCols> res;
        for (size_t i = 0; i < Rows; ++i) {
            for (size_t j = 0; j < OtherCols; ++j) {
                float sum = 0.0f;
                for (size_t k = 0; k < Cols; ++k) {
                    sum += data[i][k] * other.data[k][j];
                }
                res.data[i][j] = sum;
            }
        }
        return res;
    }

    Matrix<Rows, Cols> operator+(const Matrix<Rows, Cols>& other) const {
        Matrix<Rows, Cols> res;
        for (size_t i = 0; i < Rows; ++i) {
            for (size_t j = 0; j < Cols; ++j) {
                res.data[i][j] = data[i][j] + other.data[i][j];
            }
        }
        return res;
    }

    Matrix<Rows, Cols> operator*(float scalar) const {
        Matrix<Rows, Cols> res;
        for (size_t i = 0; i < Rows; ++i) {
            for (size_t j = 0; j < Cols; ++j) {
                res.data[i][j] = data[i][j] * scalar;
            }
        }
        return res;
    }

    Matrix<Rows, Cols> operator-(const Matrix<Rows, Cols>& other) const {
        Matrix<Rows, Cols> res;
        for (size_t i = 0; i < Rows; ++i) {
            for (size_t j = 0; j < Cols; ++j) {
                res.data[i][j] = data[i][j] - other.data[i][j];
            }
        }
        return res;
    }
};

// Simple 4x4 matrix inversion using Gauss-Jordan elimination
inline Matrix<4, 4> invert4x4(Matrix<4, 4> mat) {
    Matrix<4, 4> inv = Matrix<4, 4>::identity();
    
    for (int i = 0; i < 4; ++i) {
        // Find pivot
        float maxEl = std::abs(mat.data[i][i]);
        int maxRow = i;
        for (int k = i + 1; k < 4; ++k) {
            if (std::abs(mat.data[k][i]) > maxEl) {
                maxEl = std::abs(mat.data[k][i]);
                maxRow = k;
            }
        }
        
        // Swap rows
        if (maxRow != i) {
            std::swap(mat.data[i], mat.data[maxRow]);
            std::swap(inv.data[i], inv.data[maxRow]);
        }
        
        float pivot = mat.data[i][i];
        if (std::abs(pivot) < 1e-6f) {
            throw std::runtime_error("Matrix is singular");
        }
        
        for (int j = 0; j < 4; ++j) {
            mat.data[i][j] /= pivot;
            inv.data[i][j] /= pivot;
        }
        
        for (int k = 0; k < 4; ++k) {
            if (k != i) {
                float factor = mat.data[k][i];
                for (int j = 0; j < 4; ++j) {
                    mat.data[k][j] -= factor * mat.data[i][j];
                    inv.data[k][j] -= factor * inv.data[i][j];
                }
            }
        }
    }
    
    return inv;
}
