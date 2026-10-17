//  CSEMModel.h  —  CORE 2: Circular Sector Element Method [2] Lee et al. 2008
//                  CORE IMPROVE: (a) lan truyền biến dạng [3] Lee & Choi 2009
//                                (b) làm mượt Laplace cục bộ (cải tiến riêng)
//
//  Gốm = lưới (rows x cols) phần tử hình quạt. Phần tử S = (S_r, S_theta, S_h)
//  chỉ thay đổi S_r -> biến dạng cục bộ, không đối xứng trụ.
//  improved = false : CSEM gốc [2]
//  improved = true  : CSEM + (a) và/hoặc (b), bật/tắt trong Config.h
#ifndef CSEM_MODEL_H
#define CSEM_MODEL_H

#include "PotteryModel.h"

class CSEMModel : public PotteryModel
{
public:
    CSEMModel(int rows, int cols, bool improved);

    const char* name() const override
    {
        return m_improved ? "CSEM improved (core improve)" : "CSEM (core 2)";
    }
    void reset() override;
    StepOutput step(const StepInput& in) override;
    void copyRows(int row0, int row1, std::vector<double>& out) const override;

private:
    // chế độ va chạm của [2] mục 3
    enum Mode { CONTRACTION, EXPANSION };

    int index(int row, int col) const { return row * m_cols + col; }

    // "chỗ nối góc": đưa chỉ số cột về [0, cols-1] (cột -1 chính là cột cols-1)
    int wrapCol(int col) const
    {
        col = col % m_cols;
        if (col < 0) col += m_cols;
        return col;
    }

    // CORE IMPROVE
    void propagate();
    void smoothWindow(int i0, int i1, int j0, int j1, double dtScale);

    bool m_improved;
    double m_dh;                    // khoảng cách hàng (chiều cao)
    double m_dTheta;                // khoảng cách cột (góc) = u_theta trong [3]
    double m_cv;                    // c_v của eq.(9), mỗi phần tử

    std::vector<double> m_radius;   // S_r của mọi phần tử, m_radius[row*cols + col]
    std::vector<double> m_cos;      // cos(S_theta) mỗi cột (tính sẵn)
    std::vector<double> m_sin;      // sin(S_theta) mỗi cột

    Mode m_mode;                    // chế độ đang dùng trong lần chạm hiện tại
    bool m_inContact;               // vòng trước có va chạm không

    // dữ liệu tạm cho CORE IMPROVE
    std::vector<unsigned char> m_hit;   // 1 = phần tử bị HIP chạm trực tiếp vòng này
    std::vector<double> m_delta;        // dS_r của phần tử bị chạm vòng này
    std::vector<int>    m_hitList;      // danh sách chỉ số bị chạm
    std::vector<double> m_spread;       // biến dạng lan truyền cộng dồn
    std::vector<int>    m_spreadList;   // danh sách chỉ số nhận lan truyền
    std::vector<double> m_window;       // bản copy vùng làm mượt
};

#endif
