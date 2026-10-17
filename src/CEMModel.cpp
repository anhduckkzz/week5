//  CEMModel.cpp  —  CORE 1: Cylindrical Element Method [1]
//
//  Ký hiệu theo Hình 4 và Hình 6 của [1]:
//    r1 = bán kính HIP,  h1 = độ cao HIP,
//    r2 = bán kính trụ,  h2 = độ cao trụ,
//    r3 = khoảng cách từ HIP tới trục gốm.
#include "CEMModel.h"
#include <algorithm>

CEMModel::CEMModel()
{
    m_rows = CEM_NUM_ELEMENTS;
    m_cols = CEM_RENDER_COLS;      // chỉ dùng khi vẽ
    m_dh = POT_HEIGHT / (m_rows - 1);
    reset();
}

void CEMModel::reset()
{
    m_radius.assign(m_rows, POT_INIT_RADIUS);
    markAllDirty();
}

void CEMModel::copyRows(int row0, int row1, std::vector<double>& out) const
{
    // CEM đối xứng trụ: mọi cột của một hàng có cùng bán kính
    for (int i = row0; i <= row1; i++)
    {
        for (int j = 0; j < m_cols; j++)
        {
            out[i * m_cols + j] = m_radius[i];
        }
    }
}

StepOutput CEMModel::step(const StepInput& in)
{
    StepOutput out;
    out.force.zero();
    out.numContacts = 0;
    out.avgDepth = 0.0;
    out.maxDepth = 0.0;

    const cVector3d& H = in.hip;
    double r1 = in.hipRadius;
    double h1 = H.z();
    double r3 = sqrt(H.x() * H.x() + H.y() * H.y());

    bool rotating = fabs(in.wheelSpeed) > 1e-6;

    // [1] mục V.A: độ cứng khác nhau khi bàn quay / đứng yên
    double K = rotating ? CEM_K_ROTATING : CEM_K_STATIC;

    // CORE 1 — VA CHẠM (mục IV.A của [1])
    // Bước 1: chỉ xét các trụ có h2 trong [h1 - r1, h1 + r1]. Đổi ra dải chỉ số: O(1).
    int i0 = (int)ceil((h1 - r1) / m_dh);
    int i1 = (int)floor((h1 + r1) / m_dh);
    i0 = std::max(i0, 0);
    i1 = std::min(i1, m_rows - 1);

    cVector3d sumStiffness(0, 0, 0);
    double sumDepth = 0.0;

    for (int i = i0; i <= i1; i++)
    {
        double h2 = i * m_dh;
        double r2 = m_radius[i];

        // r1*cos(phi): bán kính lát cắt ngang của quả cầu HIP tại độ cao h2
        double dz = h1 - h2;
        double r1cos = sqrt(std::max(0.0, r1 * r1 - dz * dz));

        // Bước 2: eq.(1)  proximity = -(r1 cos + r2) + r3,  âm = va chạm
        double proximity = -(r1cos + r2) + r3;
        if (proximity >= 0.0) continue;

        double depth = -proximity;

        // CORE 1 — BIẾN DẠNG (mục IV.B của [1])
        // eq.(2): radius_t = radius_{t-1} - |proximity| / (c1 * r1)
        // [1] chỉ khoét khi bàn đang quay.
        if (rotating || !CEM_DEFORM_ONLY_WHEN_ROTATING)
        {
            double divisor = std::max(1.0, CEM_C1 * r1);       // c1*r1 luôn >= 1
            double dtScale = std::min(in.dt / DEFORM_DT_REF, 5.0);
            double newR = r2 - depth / divisor * dtScale;
            m_radius[i] = std::max(POT_MIN_RADIUS, newR);
            markDirty(i);
        }

        // CORE 1 — LỰC ĐỘ CỨNG (mục V.A của [1])
        // eq.(4): Fs_i = K |d1| (p2 - p1)/|p2 - p1|
        // [1] lấy hướng từ điểm va chạm p1 tới HIP p2. Khi tâm HIP lún vào trong
        // mặt gốm thì hướng đó bị đảo ngược, nên ta dùng hướng từ trục gốm
        // (cùng độ cao h2) tới HIP: gần như cùng hướng nhưng không bao giờ đảo.
        cVector3d dir(H.x(), H.y(), dz);
        double len = dir.length();
        if (len < 1e-9) continue;
        dir = dir / len;

        sumStiffness = sumStiffness + (K * depth) * dir;
        sumDepth += depth;
        out.maxDepth = std::max(out.maxDepth, depth);
        out.numContacts++;
    }

    if (out.numContacts == 0) return out;

    // eq.(5): Fs = (1/n) * sum(Fs_i)
    cVector3d Fs = sumStiffness / out.numContacts;

    // CORE 1 — LỰC NHỚT (mục V.B của [1])
    // eq.(6): Fv = -c2 * v1
    cVector3d Fv = -CEM_C2_VISCOSITY * in.hipVelocity;

    // CORE 1 — MA SÁT XOAY (mục V.C của [1])
    // eq.(7): Ff = c3 * v2 * r3 * ((p3 - p2)/|p3 - p2|) x n
    //   (p3 - p2) hướng từ HIP vào trục, n = trục quay (z)
    //   -> kết quả là tiếp tuyến theo chiều quay.
    cVector3d Ff(0, 0, 0);
    if (rotating && r3 > 1e-9)
    {
        cVector3d toAxis(-H.x() / r3, -H.y() / r3, 0.0);
        cVector3d n(0, 0, 1);
        cVector3d tangent = cCross(toAxis, n);
        Ff = (CEM_C3_FRICTION * in.wheelSpeed * r3) * tangent;
    }

    // eq.(3): F = Fs + Fv + Ff
    out.force = Fs + Fv + Ff;
    out.avgDepth = sumDepth / out.numContacts;
    return out;
}
