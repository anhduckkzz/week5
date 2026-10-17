//  Config.h  —  Tất cả tham số chỉnh được của bài Haptic Pottery
//
//  Sửa số ở đây rồi build lại. Đơn vị: mét, giây, Newton, radian
//  (trừ khi ghi khác).
//
//  Ký hiệu: [1] Han 2007 (CEM), [2] Lee 2008 (CSEM), [3] Lee 2009 (CSEM cải tiến)
//  Thiết bị mục tiêu: Novint Falcon (Chai3D: lực max 8 N, độ cứng max 3000 N/m,
//  giảm chấn max 20 N.s/m, bán kính workspace ~0.04 m)
#ifndef POTTERY_CONFIG_H
#define POTTERY_CONFIG_H

#include <cmath>

// Hình dạng gốm (trục gốm là trục z, đáy gốm nằm ở z = 0)
const double POT_HEIGHT        = 0.060;   // chiều cao gốm [m]
const double POT_INIT_RADIUS   = 0.020;   // bán kính ống trụ ban đầu [m]
const double POT_MIN_RADIUS    = 0.003;   // không cho co nhỏ hơn (tránh xuyên trục) [m]
const double POT_MAX_RADIUS    = 0.040;   // không cho giãn lớn hơn [m]
const double POT_WALL_VISUAL   = 0.0015;  // độ dày thành gốm CHỈ ĐỂ VẼ mặt trong [m]

// HIP (quả cầu tương tác)
const double HIP_RADIUS_INIT   = 0.004;   // [m]
const double HIP_RADIUS_MIN    = 0.0015;  // [m]
const double HIP_RADIUS_MAX    = 0.012;   // [m]
const double HIP_RADIUS_STEP   = 0.0005;  // mỗi lần bấm [ hoặc ] [m]

// Bàn xoay
const double WHEEL_SPEED_INIT  = 3.0;     // [rad/s] (~29 vòng/phút)
const double WHEEL_SPEED_STEP  = 0.5;     // mỗi lần bấm W / S [rad/s]
const double WHEEL_SPEED_MAX   = 12.0;    // [rad/s]

// Ánh xạ thiết bị sang thế giới ảo
//   HIP = vị_trí_thiết_bị * WORKSPACE_SCALE + WORKSPACE_OFFSET
//   Lưu ý: scale > 1 làm độ cứng cảm nhận ở tay tăng theo tỉ lệ.
const double WORKSPACE_SCALE    = 1.5;
const double WORKSPACE_OFFSET_X = 0.030;  // tâm workspace nằm trước mặt gốm
const double WORKSPACE_OFFSET_Y = 0.0;
const double WORKSPACE_OFFSET_Z = 0.035;

// HIP ảo (khi chưa có thiết bị): điều khiển bằng phím mũi tên và PageUp/PageDown
const double VIRTUAL_HIP_SPEED  = 0.03;   // [m/s]
const double VIRTUAL_START_X    = 0.035;  // vị trí ban đầu [m]
const double VIRTUAL_START_Z    = 0.030;
const double VIRTUAL_LOOP_HZ    = 1000.0; // nhịp vòng haptic khi không có thiết bị
                                          // (giả lập Falcon ~1 kHz). 0 = chạy tự do.

// Lực gửi về thiết bị
const double MAX_FORCE          = 6.0;    // cắt lực an toàn [N] (Falcon max 8 N)
const double FILTER_CUTOFF_HZ   = 10.0;   // Butterworth thông thấp, theo [2]
const bool   USE_FILTER_DEFAULT = true;

// Công thức biến dạng của bài báo tính theo mỗi vòng lặp ở ~1 kHz.
// Ta nhân thêm (dt / DEFORM_DT_REF) để kết quả không đổi khi vòng lặp
// chạy nhanh/chậm hơn 1 kHz.
const double DEFORM_DT_REF      = 0.001;  // [s]

// CORE 1 — CEM [1]
const int    CEM_NUM_ELEMENTS   = 600;    // số trụ theo chiều cao (60 mm -> 0.1 mm/lát)
const int    CEM_RENDER_COLS    = 90;     // số cột chỉ để vẽ (CEM đối xứng trụ)
const double CEM_K_ROTATING     = 100.0;  // độ cứng khi bàn quay [N/m]  ([1]: 0.1 N/mm)
const double CEM_K_STATIC       = 500.0;  // độ cứng khi bàn đứng yên [N/m] ([1]: 0.5 N/mm)
const double CEM_C1             = 20000.0;// eq.(2): bán kính giảm |proximity|/(c1*r1) [1/m]
                                          // (c1*r1 ~ số vòng lặp để "chảy" hết, >= 1)
const double CEM_C2_VISCOSITY   = 3.0;    // eq.(6): Fv = -c2 v  [N.s/m] (Falcon max 20)
const double CEM_C3_FRICTION    = 2.0;    // eq.(7): Ff = c3 w r3 t  [N.s/m]
const bool   CEM_DEFORM_ONLY_WHEN_ROTATING = true;  // [1]: chỉ khoét khi bàn đang quay

// CORE 2 — CSEM [2]
// Các độ phân giải (số hàng theo chiều cao x số cột theo góc), giống bảng của [2].
// Bấm phím N để chuyển.
const int    CSEM_NUM_PRESETS   = 4;
const int    CSEM_PRESET_ROWS[CSEM_NUM_PRESETS] = { 200, 200, 400, 800 };
const int    CSEM_PRESET_COLS[CSEM_NUM_PRESETS] = { 180, 360, 360, 360 };
const int    CSEM_DEFAULT_PRESET = 1;     // 200 x 360

const double CSEM_C_ETA         = 100.0;  // eq.(6): dSr = -l / c_eta  (hệ số giả nhớt)
                                          // ~ số mili-giây để đất "chảy" hết độ xuyên
// eq.(9): F_i = c_v * p_i * f_i. Paper không cho số. Ta đặt c_v = mật độ * diện tích
// một phần tử để đổi độ phân giải mà lực cảm nhận không đổi.
const double CSEM_STIFFNESS_DENSITY = 1.5e7; // [N/m^3]
const double CSEM_C_R           = 80.0;   // eq.(10): Fr = c_r * max(p) * t  [N/m]

// CORE IMPROVE — CSEM cải tiến [3] (+ cải tiến riêng)
// (a) Lan truyền biến dạng theo [3]: l = dSr / ((c_l * u_theta * S_r * n)^2 + 1)
//     ĐO THỬ (400x360, quỹ đạo phím B): chỉ bật (a) thì độ sắc RMS 6.79 -> 6.99 um
//     (tệ hơn), nên mặc định TẮT. Bật lên để so sánh trong báo cáo.
const bool   IMP_USE_PROPAGATION = false;
const double IMP_C_L            = 2000.0; // hệ số lan truyền c_l [1/m]
const double IMP_THRESHOLD      = 1e-7;   // dừng lan khi |l| nhỏ hơn [m]
const int    IMP_MAX_RING       = 8;      // số vòng vuông tối đa
// (b) Làm mượt Laplace cục bộ quanh HIP (của mình): mỗi vòng, ô trong vùng
//     quanh HIP kéo về trung bình 4 ô kề với hệ số này (0 = tắt, <= 0.25 để ổn định)
//     ĐO THỬ: 0.2 -> độ sắc RMS 6.79 -> 4.29 um (-37%), MAX 31.9 -> 17.8 um (-44%)
const double IMP_SMOOTHING      = 0.2;
const int    IMP_SMOOTH_MARGIN  = 3;      // nới vùng làm mượt ra thêm bao nhiêu ô
// (c) Chỉ cập nhật các hàng mesh bị thay đổi (thay cho GPU shader của [3])

// Thử nghiệm tự động
// Mục 5.1 (phím T): HIP ấn chậm vào gốm trong 400 ms, ghi log mỗi vòng haptic
const double TEST_DURATION      = 0.400;  // [s]
const double TEST_DEPTH         = 0.006;  // HIP đi sâu vào bao nhiêu sau 400 ms [m]
const double TEST_HEIGHT        = 0.030;  // độ cao ấn [m]
const bool   TEST_WHEEL_ON      = false;  // bàn xoay trong lúc test

// Mục 5.2 (phím B): HIP đi từ miệng xuống đáy khi bàn quay chậm (như Hình 4 của [3]).
// 1 vòng/giây trong 10 s -> 10 vòng rãnh xoắn cách nhau ~6.4 mm, hẹp hơn bề rộng
// rãnh (~6.9 mm với HIP 4 mm, sâu 2 mm) -> các vòng chồng lên nhau, để lại gờ
// giữa hai vòng: đúng chỗ bản gốc bị cạnh sắc.
const double BENCH_DURATION     = 10.0;   // [s]
const double BENCH_DEPTH        = 0.002;  // độ ăn sâu cố định [m]
const double BENCH_WHEEL_SPEED  = 6.2832; // [rad/s] (1 vòng/giây)

#endif
