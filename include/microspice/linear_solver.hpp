// linear_solver.hpp - Dense matrices and Gaussian elimination with pivoting.

#ifndef MICROSPICE_LINEAR_SOLVER_HPP
#define MICROSPICE_LINEAR_SOLVER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace microspice {

class DenseMatrix {
public:
    explicit DenseMatrix(std::size_t size)
        : size_(size), values_(size * size, 0.0) {}

    std::size_t size() const noexcept { return size_; }

    double& operator()(std::size_t row, std::size_t column) noexcept {
        return values_[row * size_ + column];
    }

    double operator()(std::size_t row, std::size_t column) const noexcept {
        return values_[row * size_ + column];
    }

    void swapRows(std::size_t first, std::size_t second) noexcept {
        for (std::size_t column = 0; column < size_; ++column) {
            std::swap((*this)(first, column), (*this)(second, column));
        }
    }

private:
    std::size_t size_;
    std::vector<double> values_;
};

/// Solve A*x=b by Gaussian elimination with partial pivoting.
///
/// Partial pivoting selects the largest available coefficient in each column.
/// This both handles zero diagonal entries and reduces floating-point error.
inline std::vector<double> solveLinearSystem(DenseMatrix matrix,
                                             std::vector<double> rhs,
                                             double pivotTolerance = 1e-12) {
    const std::size_t size = matrix.size();
    if (rhs.size() != size) {
        throw std::invalid_argument("matrix and right-hand side size mismatch");
    }

    for (std::size_t pivotColumn = 0; pivotColumn < size; ++pivotColumn) {
        std::size_t pivotRow = pivotColumn;
        double largest = std::abs(matrix(pivotRow, pivotColumn));
        for (std::size_t row = pivotColumn + 1; row < size; ++row) {
            const double candidate = std::abs(matrix(row, pivotColumn));
            if (candidate > largest) {
                largest = candidate;
                pivotRow = row;
            }
        }

        if (largest <= pivotTolerance) {
            throw std::runtime_error("singular or ill-conditioned matrix");
        }

        if (pivotRow != pivotColumn) {
            matrix.swapRows(pivotRow, pivotColumn);
            std::swap(rhs[pivotRow], rhs[pivotColumn]);
        }

        for (std::size_t row = pivotColumn + 1; row < size; ++row) {
            const double factor =
                matrix(row, pivotColumn) / matrix(pivotColumn, pivotColumn);
            matrix(row, pivotColumn) = 0.0;
            for (std::size_t column = pivotColumn + 1; column < size;
                 ++column) {
                matrix(row, column) -= factor * matrix(pivotColumn, column);
            }
            rhs[row] -= factor * rhs[pivotColumn];
        }
    }

    std::vector<double> solution(size, 0.0);
    for (std::size_t row = size; row-- > 0;) {
        double remainder = rhs[row];
        for (std::size_t column = row + 1; column < size; ++column) {
            remainder -= matrix(row, column) * solution[column];
        }
        solution[row] = remainder / matrix(row, row);
    }
    return solution;
}

}  // namespace microspice

#endif  // MICROSPICE_LINEAR_SOLVER_HPP