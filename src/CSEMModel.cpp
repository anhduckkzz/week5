//  CSEMModel.cpp  —  CORE 2: CSEM [2]  +  CORE IMPROVE: lan truyền [3]
//
//  Ký hiệu theo Hình 2 và Hình 5 của [2]:
//    H = tâm HIP, H_r = bán kính HIP, d = khoảng cách từ H tới trục gốm,
//    C = điểm trên trục ở cùng độ cao với phần tử S,
//    r = bán kính lát cắt của quả cầu HIP tại độ cao S_h,
//    tia C->S cắt lát cắt tại E1 (gần C) và E2 (xa C), M = trung điểm E1E2.
#include "CSEMModel.h"
#include <algorithm>

CSEMModel::CSEMModel(int rows, int cols, bool improved)
{
    m_rows = rows;
    m_cols = cols;
    m_improved = improved;

    m_dh = POT_HEIGHT / (m_rows - 1);
    m_dTheta = 2.0 * M_PI / m_cols;

    // c_v mỗi phần tử = mật độ * diện tích phần tử (để lực không đổi khi đổi độ phân giải)
    m_cv = CSEM_STIFFNESS_DENSITY * m_dh * (POT_INIT_RADIUS * m_dTheta);

    // tính sẵn cos/sin của góc mỗi cột
    m_cos.resize(m_cols);
    m_sin.resize(m_cols);
    for (int j = 0; j < m_cols; j++)
    {
        m_cos[j] = cos(j * m_dTheta);
        m_sin[j] = sin(j * m_dTheta);
    }

    int n = m_rows * m_cols;
    m_hit.assign(n, 0);
    m_delta.assign(n, 0.0);
    m_spread.assign(n, 0.0);

    reset();
}

void CSEMModel::reset()
{
    m_radius.assign(m_rows * m_cols, POT_INIT_RADIUS);
    m_mode = CONTRACTION;
    m_inContact = false;
    markAllDirty();
}

void CSEMModel::copyRows(int row0, int row1, std::vector<double>& out) const
{
    for (int k = row0 * m_cols; k < (row1 + 1) * m_cols; k++)
    {
        out[k] = m_radius[k];
    }
}

StepOutput CSEMModel::step(const StepInput& in)
{
    StepOutput out;
    out.force.zero();
    out.numContacts = 0;
    out.avgDepth = 0.0;
    out.maxDepth = 0.0;

    const cVector3d& H = in.hip;
    double Hr = in.hipRadius;
    double d = sqrt(H.x() * H.x() + H.y() * H.y());

    // góc của HIP, đưa về [0, 2pi) vì atan2 trả về (-pi, pi]
    double thetaH = atan2(H.y(), H.x());
    if (thetaH < 0.0) thetaH += 2.0 * M_PI;

    bool rotating = fabs(in.wheelSpeed) > 1e-6;
    double dtScale = std::min(in.dt / DEFORM_DT_REF, 5.0);

    // CORE 2 — VA CHẠM, BƯỚC 1: lọc theo dải (mục 3 của [2])
    // eq.(1): S_h trong [H_y - H_r, H_y + H_r]   (ở đây trục cao là z)
    int i0 = (int)ceil((H.z() - Hr) / m_dh);
    int i1 = (int)floor((H.z() + Hr) / m_dh);
    i0 = std::max(i0, 0);
    i1 = std::min(i1, m_rows - 1);

    // eq.(2): S_theta trong [thetaH - a, thetaH + a].
    // [2] xấp xỉ a = H_r/d; ta dùng giá trị đúng a = asin(H_r/d) (luôn >= H_r/d,
    // nên không bỏ sót phần tử khi HIP ở gần trục).
    int j0 = 0;
    int j1 = m_cols - 1;
    if (d > Hr)
    {
        double a = asin(Hr / d);
        j0 = (int)floor((thetaH - a) / m_dTheta);
        j1 = (int)ceil((thetaH + a) / m_dTheta);
        if (j1 - j0 + 1 >= m_cols) { j0 = 0; j1 = m_cols - 1; }
    }
    // (d <= Hr: HIP trùm lên trục -> xét mọi cột)

    // CHỌN CHẾ ĐỘ co / giãn (mục 3 của [2])
    // Chỉ chọn khi bắt đầu một lần chạm mới, rồi giữ nguyên tới khi nhả ra.
    // HIP ở trong lòng gốm (d < S_r) -> giãn (đẩy thành gốm ra ngoài).
    if (!m_inContact)
    {
        int ri = (int)lround(H.z() / m_dh);
        ri = std::max(0, std::min(ri, m_rows - 1));
        int cj = wrapCol((int)lround(thetaH / m_dTheta));
        m_mode = (d < m_radius[index(ri, cj)]) ? EXPANSION : CONTRACTION;
    }

    double sumDepth = 0.0;
    cVector3d sumForce(0, 0, 0);

    for (int i = i0; i <= i1; i++)
    {
        double h = i * m_dh;
        double dz = H.z() - h;
        double r2 = Hr * Hr - dz * dz;
        if (r2 <= 0.0) continue;
        double r = sqrt(r2);

        // jj có thể âm hoặc >= cols -> wrapCol() xử lý "chỗ nối góc"
        for (int jj = j0; jj <= j1; jj++)
        {
            int j = wrapCol(jj);
            double c = m_cos[j];
            double s = m_sin[j];

            // CORE 2 — VA CHẠM, BƯỚC 2 + 3: tìm E1, E2, M trên tia C->S
            double t0 = H.x() * c + H.y() * s;          // |M - C|
            double q2 = d * d - t0 * t0;                // (khoảng cách HIP -> tia)^2
            if (q2 < 0.0) q2 = 0.0;
            if (q2 >= r2) continue;                     // tia không cắt lát cắt
            double w = sqrt(r2 - q2);
            double e1 = t0 - w;                         // |E1 - C|
            double e2 = t0 + w;                         // |E2 - C|
            if (e2 <= 0.0) continue;                    // lát cắt nằm phía bên kia trục

            int k = index(i, j);
            double Sr = m_radius[k];

            bool pointCollision;
            double p;            // độ xuyên
            double l;            // độ dài cần dời để hết chạm, eq.(5)
            double contactDist;  // vị trí điểm tiếp xúc trên tia (E1, E2 hoặc M)
            double newSr;

            if (m_mode == CONTRACTION)
            {
                if (Sr <= e1) continue;                 // đầu phần tử chưa tới quả cầu
                pointCollision = (Sr < d);              // bước 2: loại va chạm

                // eq.(3)
                if (pointCollision) p = Sr - e1;
                else                p = r - sqrt(q2);

                // CORE 2 — BIẾN DẠNG (mục 4.1 của [2])
                // eq.(5): l = S_r - |E1 - C|     eq.(6): dS_r = -l / c_eta
                double e1c = std::max(e1, 0.0);
                l = Sr - e1c;
                newSr = Sr - l / CSEM_C_ETA * dtScale;
                contactDist = pointCollision ? e1c : t0;
            }
            else // EXPANSION: thay E1 bằng E2, đảo điều kiện
            {
                if (Sr >= e2) continue;
                pointCollision = (Sr > d);

                // eq.(4)
                if (pointCollision) p = e2 - Sr;
                else                p = r - sqrt(q2);

                l = e2 - Sr;
                newSr = Sr + l / CSEM_C_ETA * dtScale;
                contactDist = pointCollision ? e2 : t0;
            }

            if (p <= 0.0) continue;

            newSr = std::max(POT_MIN_RADIUS, std::min(newSr, POT_MAX_RADIUS));

            // CORE 2 — LỰC (mục 4.2 của [2])
            // eq.(8): f_i = (H - E1)/|H - E1|  (point)   hoặc (H - M)/|H - M|  (line)
            // eq.(9): F_v^i = c_v * p_i * f_i
            cVector3d contact(contactDist * c, contactDist * s, h);
            cVector3d f = H - contact;
            double len = f.length();
            if (len > 1e-9)
            {
                sumForce = sumForce + (m_cv * p / len) * f;
            }

            // áp dụng biến dạng
            m_radius[k] = newSr;
            markDirty(i);

            if (m_improved)
            {
                m_hit[k] = 1;
                m_delta[k] = newSr - Sr;
                m_hitList.push_back(k);
            }

            sumDepth += p;
            out.maxDepth = std::max(out.maxDepth, p);
            out.numContacts++;
        }
    }

    // CORE 2 — MA SÁT XOAY
    // eq.(10): F_r = c_r * max(p_i) * t   (t = tiếp tuyến theo chiều quay)
    if (rotating && out.numContacts > 0 && d > 1e-9)
    {
        double sign = (in.wheelSpeed > 0.0) ? 1.0 : -1.0;
        cVector3d t(-H.y() / d * sign, H.x() / d * sign, 0.0);
        sumForce = sumForce + (CSEM_C_R * out.maxDepth) * t;
    }

    // eq.(7): F = sum(F_v^i) + F_r
    out.force = sumForce;

    // CORE IMPROVE — lan truyền biến dạng sang phần tử lân cận [3]
    if (m_improved)
    {
        if (out.numContacts > 0)
        {
            if (IMP_USE_PROPAGATION) propagate();                       // (a)
            if (IMP_SMOOTHING > 0.0) smoothWindow(i0, i1, j0, j1, dtScale); // (b)
        }

        for (int k : m_hitList) m_hit[k] = 0;
        m_hitList.clear();
    }

    m_inContact = (out.numContacts > 0);
    if (out.numContacts > 0) out.avgDepth = sumDepth / out.numContacts;
    return out;
}

//  CORE IMPROVE — Response force propagation, mục II.B của [3]
//
//  - Chỉ phần tử ở BIÊN vùng va chạm mới lan truyền (biên = có ít nhất một ô
//    hàng xóm không bị chạm).
//  - Lan theo các "vòng vuông" (tetragonal hull) thứ n = 1, 2, ... quanh ô đó:
//        l = dS_r / ((c_l * delta)^2 + 1),   delta = u_theta * S_r * n
//  - Dừng khi |l| < ngưỡng.
//  - Khác [3] (của mình): [3] cộng dồn ảnh hưởng từ mọi phần tử biên. Khi đo
//    thử, cộng dồn làm ô ngay ngoài vùng chạm nhận phần lan truyền từ rất nhiều
//    phần tử biên, lún sâu hơn chính các ô biên -> tạo "hào" lởm chởm (bề mặt
//    SẮC hơn bản gốc). Vì vậy mỗi ô chỉ giữ ảnh hưởng LỚN NHẤT. Do l luôn nhỏ
//    hơn dS_r của phần tử nguồn nên độ lún giảm dần đều ra xa vùng chạm.
void CSEMModel::propagate()
{
    double maxAbsDelta = 0.0;
    for (int k : m_hitList) maxAbsDelta = std::max(maxAbsDelta, fabs(m_delta[k]));
    if (maxAbsDelta < IMP_THRESHOLD) return;

    for (int k : m_hitList)
    {
        int row = k / m_cols;
        int col = k % m_cols;

        // có phải phần tử biên không?
        bool boundary = false;
        if (row > 0          && !m_hit[index(row - 1, col)]) boundary = true;
        if (row < m_rows - 1 && !m_hit[index(row + 1, col)]) boundary = true;
        if (!m_hit[index(row, wrapCol(col - 1))]) boundary = true;
        if (!m_hit[index(row, wrapCol(col + 1))]) boundary = true;
        if (!boundary) continue;

        double dS = m_delta[k];
        if (fabs(dS) < IMP_THRESHOLD) continue;

        // c_l * u_theta * S_r
        double unit = IMP_C_L * m_dTheta * m_radius[k];

        // số vòng tối đa sao cho |l| còn >= ngưỡng
        int nMax = IMP_MAX_RING;
        if (unit > 0.0)
        {
            int nLimit = (int)(sqrt(fabs(dS) / IMP_THRESHOLD - 1.0) / unit);
            nMax = std::min(nMax, nLimit);
        }

        for (int n = 1; n <= nMax; n++)
        {
            double l = dS / ((unit * n) * (unit * n) + 1.0);

            // duyệt các ô trên vòng vuông thứ n
            for (int di = -n; di <= n; di++)
            {
                int rr = row + di;
                if (rr < 0 || rr >= m_rows) continue;

                // hàng trên/dưới cùng: lấy hết; các hàng giữa: chỉ 2 ô hai đầu
                int step = (di == -n || di == n) ? 1 : 2 * n;
                for (int dj = -n; dj <= n; dj += step)
                {
                    int kk = index(rr, wrapCol(col + dj));
                    if (m_hit[kk]) continue;            // ô bị chạm trực tiếp rồi

                    // giữ ảnh hưởng LỚN NHẤT (không cộng dồn, xem giải thích ở đầu hàm)
                    if (m_spread[kk] == 0.0) m_spreadList.push_back(kk);
                    if (fabs(l) > fabs(m_spread[kk])) m_spread[kk] = l;
                }
            }
        }
    }

    // áp dụng phần biến dạng lan truyền
    for (int kk : m_spreadList)
    {
        double r = m_radius[kk] + m_spread[kk];
        m_radius[kk] = std::max(POT_MIN_RADIUS, std::min(r, POT_MAX_RADIUS));
        markDirty(kk / m_cols);
        m_spread[kk] = 0.0;
    }
    m_spreadList.clear();
}

//  CORE IMPROVE (b) — làm mượt Laplace cục bộ (cải tiến riêng, không có trong [3])
//
//  Chỉ chạy trong vùng quanh HIP (dải hàng/cột của bước 1, nới thêm vài ô):
//      S_r <- S_r + lambda * (trung bình 4 ô kề - S_r)
//  Đất sét thật có độ dính giữa các phần tử kề nhau nên không giữ được gờ
//  sắc. Phép này giảm trực tiếp sai phân bậc 2 (độ "sắc") của bề mặt. Ngoài vùng
//  HIP thì không động tới, nên chi tiết đã nặn ở chỗ khác được giữ nguyên.
void CSEMModel::smoothWindow(int i0, int i1, int j0, int j1, double dtScale)
{
    double lambda = std::min(IMP_SMOOTHING * dtScale, 0.25);

    i0 = std::max(1, i0 - IMP_SMOOTH_MARGIN);
    i1 = std::min(m_rows - 2, i1 + IMP_SMOOTH_MARGIN);
    j0 -= IMP_SMOOTH_MARGIN;
    j1 += IMP_SMOOTH_MARGIN;
    if (j1 - j0 + 1 > m_cols) { j0 = 0; j1 = m_cols - 1; }
    if (i0 > i1) return;

    int w = j1 - j0 + 1;

    // copy vùng ra trước để thứ tự duyệt không ảnh hưởng kết quả
    m_window.resize((i1 - i0 + 1) * w);
    for (int i = i0; i <= i1; i++)
        for (int jj = j0; jj <= j1; jj++)
            m_window[(i - i0) * w + (jj - j0)] = m_radius[index(i, wrapCol(jj))];

    for (int i = i0; i <= i1; i++)
    {
        for (int jj = j0; jj <= j1; jj++)
        {
            int k = index(i, wrapCol(jj));
            double center = m_window[(i - i0) * w + (jj - j0)];
            double up    = m_radius[index(i + 1, wrapCol(jj))];
            double down  = m_radius[index(i - 1, wrapCol(jj))];
            double left  = m_radius[index(i, wrapCol(jj - 1))];
            double right = m_radius[index(i, wrapCol(jj + 1))];

            // dùng giá trị cũ nếu ô kề cũng nằm trong vùng
            if (i + 1 <= i1) up    = m_window[(i + 1 - i0) * w + (jj - j0)];
            if (i - 1 >= i0) down  = m_window[(i - 1 - i0) * w + (jj - j0)];
            if (jj - 1 >= j0) left  = m_window[(i - i0) * w + (jj - 1 - j0)];
            if (jj + 1 <= j1) right = m_window[(i - i0) * w + (jj + 1 - j0)];

            double average = 0.25 * (up + down + left + right);
            m_radius[k] = center + lambda * (average - center);
        }
        markDirty(i);
    }
}
