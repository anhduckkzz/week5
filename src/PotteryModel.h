//  PotteryModel.h  —  Giao diện chung cho các mô hình gốm (CEM, CSEM)
//
//  Mô hình chỉ là DỮ LIỆU (mảng bán kính). Việc vẽ nằm ở main.cpp.
//  Mọi toạ độ ở đây là HỆ TOẠ ĐỘ CỦA GỐM (đã trừ góc quay của bàn xoay):
//  trục gốm = trục z, đáy z = 0, góc theta đo trong mặt phẳng xy.
#ifndef POTTERY_MODEL_H
#define POTTERY_MODEL_H

#include "chai3d.h"
#include "Config.h"
#include <vector>

using namespace chai3d;

// Dữ liệu vào của một vòng haptic
struct StepInput
{
    cVector3d hip;          // tâm HIP (hệ gốm)
    double    hipRadius;    // bán kính HIP
    cVector3d hipVelocity;  // vận tốc HIP (hệ gốm)
    double    wheelSpeed;   // tốc độ góc bàn xoay [rad/s], 0 = đứng yên
    double    dt;           // thời gian của vòng lặp này [s]
};

// Kết quả của một vòng haptic
struct StepOutput
{
    cVector3d force;        // lực THÔ (chưa lọc), hệ gốm
    int       numContacts;  // số phần tử đang va chạm
    double    avgDepth;     // độ xuyên trung bình [m]
    double    maxDepth;     // độ xuyên lớn nhất [m]
};

class PotteryModel
{
public:
    virtual ~PotteryModel() {}

    // tên hiển thị
    virtual const char* name() const = 0;

    // đưa gốm về hình trụ ban đầu
    virtual void reset() = 0;

    // một vòng haptic: va chạm -> biến dạng -> lực
    virtual StepOutput step(const StepInput& in) = 0;

    // copy bán kính các hàng [row0..row1] ra lưới vẽ (rows x cols)
    virtual void copyRows(int row0, int row1, std::vector<double>& out) const = 0;

    // kích thước lưới vẽ
    int numRows() const { return m_rows; }
    int numCols() const { return m_cols; }

    // độ cao của hàng i
    double rowHeight(int i) const { return i * POT_HEIGHT / (m_rows - 1); }

    // theo dõi các hàng đã thay đổi (để luồng đồ hoạ chỉ cập nhật phần cần thiết)
    bool isDirty() const { return m_dirtyMin <= m_dirtyMax; }
    int  dirtyMin() const { return m_dirtyMin; }
    int  dirtyMax() const { return m_dirtyMax; }
    void clearDirty() { m_dirtyMin = m_rows; m_dirtyMax = -1; }
    void markAllDirty() { m_dirtyMin = 0; m_dirtyMax = m_rows - 1; }

protected:
    void markDirty(int row)
    {
        if (row < m_dirtyMin) m_dirtyMin = row;
        if (row > m_dirtyMax) m_dirtyMax = row;
    }

    int m_rows = 0;
    int m_cols = 0;
    int m_dirtyMin = 0;
    int m_dirtyMax = -1;
};

#endif
