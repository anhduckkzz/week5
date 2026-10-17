//  ForceFilter.h  —  CORE 2: lọc lực Butterworth thông thấp bậc 2
//
//  [2] mục 5: số phần tử va chạm thay đổi liên tục giữa các vòng lặp làm lực
//  bị giật, nên lọc lực bằng Butterworth thông thấp, tần số cắt 10 Hz.
//
//  Hệ số tính bằng biến đổi song tuyến (bilinear transform). Hệ số được tính
//  lại theo dt thực của mỗi vòng, vì vòng haptic không chạy đúng 1 kHz.
#ifndef FORCE_FILTER_H
#define FORCE_FILTER_H

#include "chai3d.h"
#include <cmath>

using namespace chai3d;

class ForceFilter
{
public:
    ForceFilter() { reset(); }

    void reset()
    {
        m_x1.zero(); m_x2.zero();
        m_y1.zero(); m_y2.zero();
    }

    // x = lực thô, trả về lực đã lọc
    cVector3d filter(const cVector3d& x, double cutoffHz, double dt)
    {
        // giữ dt trong khoảng hợp lý (tránh chia 0 / lần đầu chạy)
        if (dt < 1e-5) dt = 1e-5;
        if (dt > 5e-3) dt = 5e-3;

        // Butterworth bậc 2
        double K = tan(M_PI * cutoffHz * dt);
        double norm = 1.0 / (1.0 + sqrt(2.0) * K + K * K);
        double b0 = K * K * norm;
        double b1 = 2.0 * b0;
        double b2 = b0;
        double a1 = 2.0 * (K * K - 1.0) * norm;
        double a2 = (1.0 - sqrt(2.0) * K + K * K) * norm;

        // y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
        cVector3d y = b0 * x + b1 * m_x1 + b2 * m_x2 - a1 * m_y1 - a2 * m_y2;

        m_x2 = m_x1; m_x1 = x;
        m_y2 = m_y1; m_y1 = y;
        return y;
    }

private:
    cVector3d m_x1, m_x2;   // đầu vào cũ
    cVector3d m_y1, m_y2;   // đầu ra cũ
};

#endif
