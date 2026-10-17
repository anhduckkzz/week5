//  main.cpp  —  Haptic Pottery (bài tập 5, Robotics & Haptics VR)
//
//  Khung cửa sổ / camera / luồng haptic dựa trên ví dụ 01-mydevice của CHAI3D
//  (BSD license, (c) 2003-2024 CHAI3D) -> phải liệt kê trong báo cáo.
//
//  Kiến trúc (Hình 1 trong đề):
//    - Luồng ĐỒ HOẠ (~60 Hz): renderGraphics()  — chỉ đọc mô hình rồi vẽ.
//    - Luồng HAPTIC (~1 kHz): renderHaptics()   — đọc vị trí, va chạm,
//      biến dạng, tính + lọc lực, gửi về thiết bị.
//    - Hai luồng dùng chung g_model và các biến g_*, luôn truy cập trong
//      g_lock. Luồng đồ hoạ chỉ giữ khoá trong lúc copy mảng bán kính
//      (rất ngắn), phần dựng mesh làm SAU khi đã nhả khoá.
//
//  Mô hình:  phím 1 = CEM (core 1), 2 = CSEM (core 2), 3 = CSEM cải tiến (core improve)

#include "chai3d.h"
#include <GLFW/glfw3.h>

#include "Config.h"
#include "CEMModel.h"
#include "CSEMModel.h"
#include "ForceFilter.h"

#include <fstream>
#include <vector>
#include <deque>
#include <string>
#include <ctime>

using namespace chai3d;
using namespace std;

// Cửa sổ và camera (giữ nguyên như 01-mydevice)
cStereoMode stereoMode = C_STEREO_DISABLED;
bool fullscreen = false;
bool mirroredDisplay = false;

cWorld* world;
cCamera* camera;
cViewport* viewport = nullptr;
cDirectionalLight* light;

cHapticDeviceHandler* handler;
cGenericHapticDevicePtr hapticDevice;
bool hasDevice = false;               // có thiết bị haptic thật không

cFontPtr font;
vector<cLabel*> labels;               // các dòng chữ trên màn hình

bool simulationRunning = false;
bool simulationFinished = true;

cFrequencyCounter freqCounterGraphics;
cFrequencyCounter freqCounterHaptics;

cThread* hapticsThread;
GLFWwindow* window = nullptr;
int windowW = 0, windowH = 0;
int framebufferW = 0, framebufferH = 0;
int swapInterval = 1;

// Vật thể trong cảnh
cMesh* potMesh;                       // gốm (vẽ từ mảng bán kính)
cShapeSphere* hipSphere;              // HIP
cShapeCylinder* turntable;            // bàn xoay

// Trạng thái dùng chung giữa hai luồng, chỉ đọc/ghi khi đang giữ g_lock
cMutex g_lock;

PotteryModel* g_model = nullptr;
int g_modelType = 2;                  // 1 = CEM, 2 = CSEM, 3 = CSEM cải tiến
int g_preset = CSEM_DEFAULT_PRESET;   // độ phân giải CSEM

// do luồng đồ hoạ (người dùng) ghi
bool      g_wheelOn = false;
double    g_wheelSpeed = WHEEL_SPEED_INIT;
double    g_hipRadius = HIP_RADIUS_INIT;
bool      g_useFilter = USE_FILTER_DEFAULT;
bool      g_useDevice = false;
cVector3d g_virtualVel(0, 0, 0);      // vận tốc HIP ảo theo phím

// do luồng haptic ghi
cVector3d g_hipWorld(VIRTUAL_START_X, 0, VIRTUAL_START_Z);
double    g_phi = 0.0;                // góc quay hiện tại của bàn xoay
cVector3d g_force(0, 0, 0);           // lực đã lọc (đang gửi về tay)
int       g_contacts = 0;
double    g_avgDepth = 0.0;
double    g_computeAvgMs = 0.0;       // thời gian va chạm+biến dạng+lực (TB 0.5 s)
double    g_computeMaxMs = 0.0;

// thống kê đơn giản
struct Stats
{
    double sum, minV, maxV;
    long count;
    Stats() { reset(); }
    void reset() { sum = 0; minV = 1e30; maxV = 0; count = 0; }
    void add(double v) { sum += v; if (v < minV) minV = v; if (v > maxV) maxV = v; count++; }
    double avg() const { return count > 0 ? sum / count : 0.0; }
    double minOr0() const { return count > 0 ? minV : 0.0; }
};

// Thử nghiệm tự động (mục 5.1 và 5.2 của đề)
enum ScriptType { SCRIPT_NONE, SCRIPT_TEST, SCRIPT_BENCH };
ScriptType g_script = SCRIPT_NONE;        // đang chạy
ScriptType g_scriptRequest = SCRIPT_NONE; // đồ hoạ yêu cầu bắt đầu
ScriptType g_scriptDone = SCRIPT_NONE;    // haptic báo đã xong
double     g_scriptTime = 0.0;

struct LogRow { double t; int n; double depth; double fRaw; double fFilt; };
vector<LogRow> g_testLog;                 // log Mục 5.1

Stats g_benchCompute;                     // thời gian tính của mô hình [ms]
Stats g_benchLoop;                        // chu kỳ vòng haptic [ms]
Stats g_benchStep;                        // từ lúc chờ khoá tới lúc nhả khoá [ms]

// Chỉ luồng đồ hoạ dùng
vector<double> renderRadii;               // bản copy bán kính để vẽ
int meshRows = 0, meshCols = 0;
vector<double> meshCos, meshSin;
bool meshNeedsRebuild = true;

Stats graphicsCopy;                       // thời gian giữ khoá để copy [ms]
Stats graphicsMesh;                       // thời gian cập nhật mesh [ms]

// hàng đợi việc tự động (phím B có Shift, hoặc chạy với tham số "auto")
struct Job { int modelType; int preset; ScriptType script; };
deque<Job> jobQueue;
bool autoExit = false;
int screenshotCountdown = -1;
string screenshotName;
string statusText = "";

// Khai báo hàm
void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height);
void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height);
void onErrorCallback(int a_error, const char* a_description);
void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods);
void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale);
void renderGraphics(void);
void renderHaptics(void);
void close(void);

PotteryModel* createModel(int type, int preset);
void switchModel(int type, int preset);
void buildMesh(int rows, int cols);
void updateMeshRows(int row0, int row1);
void requestScript(ScriptType type);
void handleScriptDone(ScriptType type);
void startNextJob();
void queueFullSuite(bool withTests);
string modelTag(int type);
void saveScreenshot(const string& filename);

// MAIN
int main(int argc, char* argv[])
{
    cout << endl;
    cout << "-----------------------------------" << endl;
    cout << "HAPTIC POTTERY  (CHAI3D 3.3.0)" << endl;
    cout << "-----------------------------------" << endl << endl;
    cout << "[1] CEM (core 1)   [2] CSEM (core 2)   [3] CSEM improved (core improve)" << endl;
    cout << "[N] doi do phan giai CSEM      [R] reset gom" << endl;
    cout << "[SPACE] ban xoay quay/dung     [W]/[S] tang/giam toc do ban xoay" << endl;
    cout << "[ [ ]/[ ] ] giam/tang ban kinh HIP" << endl;
    cout << "[L] bat/tat bo loc luc         [V] doi thiet bi <-> HIP ao" << endl;
    cout << "Mui ten + PageUp/PageDown: di chuyen HIP ao" << endl;
    cout << "[T] test Muc 5.1 (log luc)     [B] benchmark Muc 5.2" << endl;
    cout << "[Shift+B] chay het bo benchmark  [P] chup man hinh" << endl;
    cout << "[F] toan man hinh  [Q]/[ESC] thoat" << endl << endl;

    // chạy với tham số "auto": tự chạy toàn bộ test + benchmark rồi thoát
    //                   "tests": chỉ chạy 3 test Mục 5.1 rồi thoát
    bool onlyTests = false;
    if (argc > 1 && string(argv[1]) == "auto") autoExit = true;
    if (argc > 1 && string(argv[1]) == "tests") { autoExit = true; onlyTests = true; }

    // Cửa sổ OpenGL (như 01-mydevice)
    if (!glfwInit())
    {
        cout << "failed initialization" << endl;
        cSleepMs(1000);
        return 1;
    }
    glfwSetErrorCallback(onErrorCallback);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    windowW = 0.8 * mode->height;
    windowH = 0.6 * mode->height;
    int x = 0.5 * (mode->width - windowW);
    int y = 0.5 * (mode->height - windowH);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_STEREO, GL_FALSE);

    window = glfwCreateWindow(windowW, windowH, "Haptic Pottery", NULL, NULL);
    if (!window)
    {
        cout << "failed to create window" << endl;
        cSleepMs(1000);
        glfwTerminate();
        return 1;
    }

    glfwSetKeyCallback(window, onKeyCallback);
    glfwSetWindowSizeCallback(window, onWindowSizeCallback);
    glfwSetFramebufferSizeCallback(window, onFrameBufferSizeCallback);
    glfwSetWindowContentScaleCallback(window, onWindowContentScaleCallback);
    glfwGetFramebufferSize(window, &framebufferW, &framebufferH);
    glfwSetWindowPos(window, x, y);
    glfwSetWindowSize(window, windowW, windowH);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(swapInterval);

#ifdef GLEW_VERSION
    if (glewInit() != GLEW_OK)
    {
        cout << "failed to initialize GLEW library" << endl;
        glfwTerminate();
        return 1;
    }
#endif

    // Thế giới, camera và ánh sáng
    world = new cWorld();
    world->m_backgroundColor.setBlack();

    camera = new cCamera(world);
    world->addChild(camera);
    camera->set(cVector3d(0.20, 0.0, 0.10),     // mắt
                cVector3d(0.0, 0.0, 0.026),     // nhìn vào giữa gốm
                cVector3d(0.0, 0.0, 1.0));      // trục z hướng lên
    camera->setClippingPlanes(0.01, 5.0);
    camera->setStereoMode(stereoMode);
    camera->setMirrorVertical(mirroredDisplay);

    light = new cDirectionalLight(world);
    world->addChild(light);
    light->setEnabled(true);
    light->setDir(-1.0, 0.3, -0.6);

    // Bàn xoay, gốm và HIP
    turntable = new cShapeCylinder(0.045, 0.045, 0.006);
    world->addChild(turntable);
    turntable->setLocalPos(0.0, 0.0, -0.006);
    turntable->m_material->setGrayDim();

    // vạch đánh dấu trên bàn xoay để nhìn thấy nó quay
    cShapeBox* marker = new cShapeBox(0.008, 0.004, 0.001);
    turntable->addChild(marker);
    marker->setLocalPos(0.038, 0.0, 0.0065);
    marker->m_material->setRedCrimson();

    potMesh = new cMesh();
    world->addChild(potMesh);
    potMesh->m_material->setColorf(0.80f, 0.60f, 0.45f);   // màu đất sét
    potMesh->m_material->setShininess(20);
    potMesh->setUseVertexColors(true);
    potMesh->setUseCulling(true);

    hipSphere = new cShapeSphere(HIP_RADIUS_INIT);
    world->addChild(hipSphere);
    hipSphere->m_material->setWhite();

    // Thiết bị haptic
    handler = new cHapticDeviceHandler();
    hasDevice = (handler->getNumDevices() > 0);
    handler->getDevice(hapticDevice, 0);

    string deviceName = "khong co thiet bi -> HIP ao (ban phim)";
    if (hasDevice)
    {
        hapticDevice->open();
        hapticDevice->calibrate();
        deviceName = hapticDevice->getSpecifications().m_modelName;
    }
    g_useDevice = hasDevice;
    cout << "Thiet bi: " << deviceName << endl;

    // Mô hình gốm ban đầu
    g_model = createModel(g_modelType, g_preset);
    buildMesh(g_model->numRows(), g_model->numCols());

    // Chữ trên màn hình
    font = NEW_CFONT_CONSOLAS_16();
    for (int i = 0; i < 7; i++)
    {
        cLabel* label = new cLabel(font);
        label->m_fontColor.setWhite();
        camera->m_frontLayer->addChild(label);
        labels.push_back(label);
    }

    float contentScaleW, contentScaleH;
    glfwGetWindowContentScale(window, &contentScaleW, &contentScaleH);
    viewport = new cViewport(camera, contentScaleW, contentScaleH);

    if (onlyTests)
    {
        jobQueue.push_back({ 1, g_preset, SCRIPT_TEST });
        jobQueue.push_back({ 2, CSEM_DEFAULT_PRESET, SCRIPT_TEST });
        jobQueue.push_back({ 3, CSEM_DEFAULT_PRESET, SCRIPT_TEST });
    }
    else if (autoExit) queueFullSuite(true);

    // Chạy luồng haptic
    hapticsThread = new cThread();
    hapticsThread->start(renderHaptics, CTHREAD_PRIORITY_HAPTICS);
    atexit(close);

    // Vòng lặp đồ hoạ
    while (!glfwWindowShouldClose(window))
    {
        renderGraphics();
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Tạo và đổi mô hình
PotteryModel* createModel(int type, int preset)
{
    if (type == 1) return new CEMModel();                                   // CORE 1
    bool improved = (type == 3);                                            // CORE 2 / IMPROVE
    return new CSEMModel(CSEM_PRESET_ROWS[preset], CSEM_PRESET_COLS[preset], improved);
}

void switchModel(int type, int preset)
{
    g_lock.acquire();
    delete g_model;
    g_model = createModel(type, preset);
    g_modelType = type;
    g_preset = preset;
    g_lock.release();

    meshNeedsRebuild = true;
}

string modelTag(int type)
{
    if (type == 1) return "CEM";
    if (type == 2) return "CSEM";
    return "CSEMimproved";
}

// Dựng mesh từ mảng bán kính (luồng đồ hoạ)
//
//  Mesh gồm: mặt ngoài (rows x cols đỉnh), mặt trong (rows x cols đỉnh,
//  cách mặt ngoài POT_WALL_VISUAL — chỉ để nhìn thấy lòng gốm), và vành
//  miệng gốm (2 x cols đỉnh, pháp tuyến hướng lên).
int outerIndex(int i, int j) { return i * meshCols + j; }
int innerIndex(int i, int j) { return meshRows * meshCols + i * meshCols + j; }
int rimOuterIndex(int j)     { return 2 * meshRows * meshCols + j; }
int rimInnerIndex(int j)     { return 2 * meshRows * meshCols + meshCols + j; }

void buildMesh(int rows, int cols)
{
    potMesh->clear();
    meshRows = rows;
    meshCols = cols;
    renderRadii.assign(rows * cols, POT_INIT_RADIUS);

    meshCos.resize(cols);
    meshSin.resize(cols);
    for (int j = 0; j < cols; j++)
    {
        meshCos[j] = cos(j * 2.0 * M_PI / cols);
        meshSin[j] = sin(j * 2.0 * M_PI / cols);
    }

    // tạo đỉnh
    int numVertices = 2 * rows * cols + 2 * cols;
    for (int k = 0; k < numVertices; k++)
    {
        potMesh->newVertex(0.0, 0.0, 0.0);
    }

    // tô một sọc tối hơn để nhìn thấy gốm đang quay
    cColorf clay(0.80f, 0.60f, 0.45f);
    cColorf stripe(0.55f, 0.38f, 0.28f);
    for (int i = 0; i < rows; i++)
    {
        for (int j = 0; j < cols; j++)
        {
            cColorf color = (j < cols / 36) ? stripe : clay;
            potMesh->m_vertices->setColor(outerIndex(i, j), color);
            potMesh->m_vertices->setColor(innerIndex(i, j), clay);
        }
    }
    for (int j = 0; j < cols; j++)
    {
        potMesh->m_vertices->setColor(rimOuterIndex(j), clay);
        potMesh->m_vertices->setColor(rimInnerIndex(j), clay);
    }

    // tạo tam giác (cột cuối nối với cột 0 -> không có khe hở ở "chỗ nối góc")
    for (int i = 0; i < rows - 1; i++)
    {
        for (int j = 0; j < cols; j++)
        {
            int jn = (j + 1) % cols;

            // mặt ngoài: pháp tuyến hướng ra
            int a = outerIndex(i, j), b = outerIndex(i, jn);
            int c = outerIndex(i + 1, jn), d = outerIndex(i + 1, j);
            potMesh->newTriangle(a, b, c);
            potMesh->newTriangle(a, c, d);

            // mặt trong: đảo chiều để pháp tuyến hướng vào lòng gốm
            a = innerIndex(i, j); b = innerIndex(i, jn);
            c = innerIndex(i + 1, jn); d = innerIndex(i + 1, j);
            potMesh->newTriangle(a, c, b);
            potMesh->newTriangle(a, d, c);
        }
    }

    // vành miệng gốm
    for (int j = 0; j < cols; j++)
    {
        int jn = (j + 1) % cols;
        potMesh->newTriangle(rimOuterIndex(j), rimOuterIndex(jn), rimInnerIndex(jn));
        potMesh->newTriangle(rimOuterIndex(j), rimInnerIndex(jn), rimInnerIndex(j));
    }

    updateMeshRows(0, rows - 1);
    potMesh->markForUpdate(false);
    meshNeedsRebuild = false;
}

// cập nhật vị trí + pháp tuyến các hàng [row0..row1] từ renderRadii
void updateMeshRows(int row0, int row1)
{
    // pháp tuyến dùng hàng kề bên -> cập nhật thêm 1 hàng mỗi phía
    int lo = max(0, row0 - 1);
    int hi = min(meshRows - 1, row1 + 1);

    double dh = POT_HEIGHT / (meshRows - 1);
    double dTheta = 2.0 * M_PI / meshCols;

    for (int i = lo; i <= hi; i++)
    {
        double h = i * dh;
        int iu = min(i + 1, meshRows - 1);
        int id = max(i - 1, 0);

        for (int j = 0; j < meshCols; j++)
        {
            int jp = (j + 1) % meshCols;
            int jm = (j - 1 + meshCols) % meshCols;

            double R  = renderRadii[i * meshCols + j];
            double dRdTheta = (renderRadii[i * meshCols + jp] - renderRadii[i * meshCols + jm]) / (2.0 * dTheta);
            double dRdz = (renderRadii[iu * meshCols + j] - renderRadii[id * meshCols + j]) / ((iu - id) * dh);

            double c = meshCos[j];
            double s = meshSin[j];

            // mặt P(theta, z) = (R cos, R sin, z)
            // dP/dtheta = (a, b, 0),  dP/dz = (e, f, 1),  pháp tuyến = dP/dtheta x dP/dz
            double a = dRdTheta * c - R * s;
            double b = dRdTheta * s + R * c;
            double e = dRdz * c;
            double f = dRdz * s;
            double nx = b;
            double ny = -a;
            double nz = a * f - b * e;
            double len = sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-12) { nx /= len; ny /= len; nz /= len; }

            potMesh->m_vertices->setLocalPos(outerIndex(i, j), R * c, R * s, h);
            potMesh->m_vertices->setNormal(outerIndex(i, j), nx, ny, nz);

            double Ri = max(R - POT_WALL_VISUAL, 0.0005);
            potMesh->m_vertices->setLocalPos(innerIndex(i, j), Ri * c, Ri * s, h);
            potMesh->m_vertices->setNormal(innerIndex(i, j), -nx, -ny, -nz);

            if (i == meshRows - 1)
            {
                potMesh->m_vertices->setLocalPos(rimOuterIndex(j), R * c, R * s, h);
                potMesh->m_vertices->setNormal(rimOuterIndex(j), 0, 0, 1);
                potMesh->m_vertices->setLocalPos(rimInnerIndex(j), Ri * c, Ri * s, h);
                potMesh->m_vertices->setNormal(rimInnerIndex(j), 0, 0, 1);
            }
        }
    }
}

// Vòng đồ hoạ
void renderGraphics(void)
{
    if (viewport == nullptr) { return; }

    // việc tự động tiếp theo (nếu có)
    startNextJob();

    // dựng lại mesh khi vừa đổi mô hình / độ phân giải
    if (meshNeedsRebuild)
    {
        g_lock.acquire();
        int rows = g_model->numRows();
        int cols = g_model->numCols();
        g_lock.release();

        buildMesh(rows, cols);

        g_lock.acquire();
        g_model->markAllDirty();
        g_lock.release();
    }

    // 1. Vận tốc HIP ảo theo phím đang giữ
    cVector3d vel(0, 0, 0);
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)        vel.x(vel.x() - VIRTUAL_HIP_SPEED);
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)      vel.x(vel.x() + VIRTUAL_HIP_SPEED);
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)      vel.y(vel.y() - VIRTUAL_HIP_SPEED);
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)     vel.y(vel.y() + VIRTUAL_HIP_SPEED);
    if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS)   vel.z(vel.z() + VIRTUAL_HIP_SPEED);
    if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) vel.z(vel.z() - VIRTUAL_HIP_SPEED);

    // 2. Trao đổi dữ liệu với luồng haptic (giữ khoá thật ngắn)
    cPrecisionClock clock;
    clock.start(true);

    g_lock.acquire();
    double lockStart = clock.getCurrentTimeSeconds();

    g_virtualVel = vel;

    cVector3d hip = g_hipWorld;
    double phi = g_phi;
    double hipRadius = g_hipRadius;
    cVector3d force = g_force;
    int contacts = g_contacts;
    double avgDepth = g_avgDepth;
    double computeAvg = g_computeAvgMs;
    double computeMax = g_computeMaxMs;
    bool wheelOn = g_wheelOn;
    double wheelSpeed = g_wheelSpeed;
    bool useFilter = g_useFilter;
    bool useDevice = g_useDevice;
    bool benchRunning = (g_script == SCRIPT_BENCH);
    bool scriptRunning = (g_script != SCRIPT_NONE);
    string modelName = g_model->name();
    int rows = g_model->numRows();
    int cols = g_model->numCols();

    // copy bán kính đã thay đổi
    int row0 = 0, row1 = -1;
    if (g_model->isDirty())
    {
        if (g_modelType == 3)
        {
            // CORE IMPROVE (b): chỉ copy + cập nhật các hàng bị thay đổi
            row0 = g_model->dirtyMin();
            row1 = g_model->dirtyMax();
        }
        else
        {
            // bản gốc: copy + cập nhật toàn bộ gốm mỗi khi có thay đổi
            row0 = 0;
            row1 = rows - 1;
        }
        g_model->copyRows(row0, row1, renderRadii);
        g_model->clearDirty();
    }

    ScriptType done = g_scriptDone;
    g_scriptDone = SCRIPT_NONE;

    double copyMs = (clock.getCurrentTimeSeconds() - lockStart) * 1000.0;
    g_lock.release();

    // 3. Cập nhật mesh (đã nhả khoá -> không làm chậm luồng haptic)
    double meshStart = clock.getCurrentTimeSeconds();
    if (row1 >= row0)
    {
        updateMeshRows(row0, row1);
    }
    double meshMs = (clock.getCurrentTimeSeconds() - meshStart) * 1000.0;

    if (benchRunning)
    {
        graphicsCopy.add(copyMs);
        graphicsMesh.add(meshMs);
    }

    if (done != SCRIPT_NONE) handleScriptDone(done);

    // 4. Đặt vị trí các vật thể
    cMatrix3d rot;
    rot.setAxisAngleRotationRad(cVector3d(0, 0, 1), phi);
    potMesh->setLocalRot(rot);
    turntable->setLocalRot(rot);

    hipSphere->setLocalPos(hip);
    hipSphere->setRadius(hipRadius);

    // 5. Chữ trên màn hình
    int displayW = viewport->getDisplayWidth();
    int displayH = viewport->getDisplayHeight();

    labels[0]->setText("Model: " + modelName + "   grid " + cStr(rows) + " x " + cStr(cols) +
                       (g_modelType == 1 ? "" : "  (" + cStr(rows * cols) + " elements)"));
    labels[1]->setText("Wheel: " + string(wheelOn ? "ON " : "OFF") + " " + cStr(wheelSpeed, 1) + " rad/s" +
                       "   HIP radius: " + cStr(hipRadius * 1000.0, 1) + " mm" +
                       "   Filter: " + string(useFilter ? "ON" : "OFF") +
                       "   Input: " + string(useDevice ? "device" : "virtual (arrows, PgUp/PgDn)"));
    labels[2]->setText("Haptic: " + cStr(freqCounterHaptics.getFrequency(), 0) + " Hz" +
                       "   compute avg " + cStr(computeAvg, 3) + " ms  max " + cStr(computeMax, 3) + " ms" +
                       "   |   Graphic: " + cStr(freqCounterGraphics.getFrequency(), 0) + " Hz");
    labels[3]->setText("Contacts: " + cStr(contacts) +
                       "   avg depth: " + cStr(avgDepth * 1000.0, 2) + " mm" +
                       "   |F|: " + cStr(force.length(), 2) + " N");
    labels[4]->setText(scriptRunning ? "AUTO SCRIPT RUNNING..." : statusText);
    labels[5]->setText("[1][2][3] model  [N] resolution  [R] reset  [SPACE] wheel  [W/S] speed  [ [ ] ] HIP size");
    labels[6]->setText("[L] filter  [V] device/virtual  [T] test 5.1  [B] bench 5.2  [Shift+B] full suite  [P] screenshot");

    for (int i = 0; i < 5; i++) labels[i]->setLocalPos(15, displayH - 25 - 20 * i);
    labels[5]->setLocalPos(15, 30);
    labels[6]->setLocalPos(15, 10);

    // 6. Vẽ
    world->updateShadowMaps(false, mirroredDisplay);
    viewport->renderView(framebufferW, framebufferH);
    glFinish();

    // chụp ảnh (đợi vài khung hình để mesh kịp cập nhật xong)
    if (screenshotCountdown > 0) screenshotCountdown--;
    else if (screenshotCountdown == 0)
    {
        saveScreenshot(screenshotName);
        screenshotCountdown = -1;
    }

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) cout << "Error: " << gluErrorString(error) << endl;

    glfwSwapBuffers(window);
    freqCounterGraphics.signal(1);

    // chế độ "auto": hết việc thì thoát
    if (autoExit && jobQueue.empty() && !scriptRunning && done == SCRIPT_NONE && screenshotCountdown < 0)
    {
        g_lock.acquire();
        bool idle = (g_script == SCRIPT_NONE && g_scriptRequest == SCRIPT_NONE);
        g_lock.release();
        if (idle) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

// Vòng haptic
void renderHaptics(void)
{
    simulationRunning = true;
    simulationFinished = false;

    cPrecisionClock clock;
    clock.start(true);
    double lastTime = clock.getCurrentTimeSeconds();

    cVector3d virtualPos(VIRTUAL_START_X, 0.0, VIRTUAL_START_Z);
    cVector3d lastHip = virtualPos;
    cVector3d hipVel(0, 0, 0);
    ForceFilter filter;
    double phi = 0.0;

    bool useDevice = false;
    bool lastButton = false;
    ScriptType lastScript = SCRIPT_NONE;
    double lastComputeMs = 0.0;
    double lastStepMs = 0.0;
    double lastLoopMs = 0.0;

    Stats windowCompute;
    double windowStart = lastTime;

    while (simulationRunning)
    {
        double loopStart = clock.getCurrentTimeSeconds();
        double dt = loopStart - lastTime;
        lastTime = loopStart;
        if (dt > 0.005) dt = 0.005;   // tránh nhảy lớn (lần chạy đầu, máy bị treo...)
        if (dt < 1e-6) dt = 1e-6;

        // Bước 1: đọc vị trí thiết bị (ngoài khoá)
        cVector3d devicePos(0, 0, 0);
        bool button = false;
        if (useDevice)
        {
            hapticDevice->getPosition(devicePos);
            hapticDevice->getUserSwitch(0, button);
        }

        double stepStart = clock.getCurrentTimeSeconds();
        g_lock.acquire();

        // thống kê của vòng trước
        windowCompute.add(lastComputeMs);
        if (g_script == SCRIPT_BENCH)
        {
            g_benchCompute.add(lastComputeMs);
            g_benchStep.add(lastStepMs);
            g_benchLoop.add(lastLoopMs);
        }
        if (loopStart - windowStart > 0.5)
        {
            g_computeAvgMs = windowCompute.avg();
            g_computeMaxMs = windowCompute.maxV;
            windowCompute.reset();
            windowStart = loopStart;
        }

        useDevice = g_useDevice;

        // nút 0 trên tay cầm Falcon: bật/tắt bàn xoay
        if (button && !lastButton) g_wheelOn = !g_wheelOn;
        lastButton = button;

        // Bắt đầu thử nghiệm tự động nếu được yêu cầu
        if (g_scriptRequest != SCRIPT_NONE && g_script == SCRIPT_NONE)
        {
            g_script = g_scriptRequest;
            g_scriptRequest = SCRIPT_NONE;
            g_scriptTime = 0.0;
            g_model->reset();
            phi = 0.0;
            filter.reset();
            if (g_script == SCRIPT_TEST)
            {
                g_testLog.clear();
                g_testLog.reserve(20000);
            }
            else
            {
                g_benchCompute.reset();
                g_benchStep.reset();
                g_benchLoop.reset();
            }
        }

        // Vị trí HIP trong thế giới
        double hipR = g_hipRadius;
        double omega = g_wheelOn ? g_wheelSpeed : 0.0;
        cVector3d hip;

        if (g_script == SCRIPT_TEST)
        {
            // Mục 5.1: ấn chậm theo phương bán kính, bắt đầu từ lúc vừa chạm mặt gốm
            double x = POT_INIT_RADIUS + hipR - TEST_DEPTH * (g_scriptTime / TEST_DURATION);
            hip.set(x, 0.0, TEST_HEIGHT);
            omega = TEST_WHEEL_ON ? g_wheelSpeed : 0.0;
        }
        else if (g_script == SCRIPT_BENCH)
        {
            // Mục 5.2: đi từ miệng gốm xuống đáy, ăn sâu cố định, bàn quay chậm
            double x = POT_INIT_RADIUS + hipR - BENCH_DEPTH;
            double z = (POT_HEIGHT + hipR) * (1.0 - g_scriptTime / BENCH_DURATION);
            hip.set(x, 0.0, z);
            omega = BENCH_WHEEL_SPEED;
        }
        else if (useDevice)
        {
            hip = WORKSPACE_SCALE * devicePos +
                  cVector3d(WORKSPACE_OFFSET_X, WORKSPACE_OFFSET_Y, WORKSPACE_OFFSET_Z);
        }
        else
        {
            virtualPos = virtualPos + dt * g_virtualVel;
            virtualPos.x(cClamp(virtualPos.x(), -0.08, 0.08));
            virtualPos.y(cClamp(virtualPos.y(), -0.08, 0.08));
            virtualPos.z(cClamp(virtualPos.z(), -0.01, 0.10));
            hip = virtualPos;
        }

        // vận tốc HIP (sai phân, làm mượt nhẹ).
        // Khi kịch bản bắt đầu / kết thúc, HIP "nhảy" tức thời sang vị trí khác:
        // không tính vận tốc cho bước nhảy đó (nếu không lực nhớt CEM sẽ vọt lên).
        if (g_script != lastScript)
        {
            lastHip = hip;
            hipVel.zero();
            lastScript = g_script;
        }
        cVector3d rawVel = (hip - lastHip) / dt;
        hipVel = 0.9 * hipVel + 0.1 * rawVel;
        lastHip = hip;

        // góc quay bàn xoay
        phi += omega * dt;
        if (phi > 2.0 * M_PI) phi -= 2.0 * M_PI;
        if (phi < 0.0) phi += 2.0 * M_PI;

        // Đổi HIP sang hệ toạ độ của gốm (xoay ngược -phi quanh z).
        // Gốm "đứng yên" trong hệ của nó, HIP quay quanh nó -> HIP đứng yên
        // vẫn khoét được rãnh quanh thân gốm.
        double cs = cos(phi);
        double sn = sin(phi);

        StepInput in;
        in.hip.set(cs * hip.x() + sn * hip.y(), -sn * hip.x() + cs * hip.y(), hip.z());
        in.hipVelocity.set(cs * hipVel.x() + sn * hipVel.y(), -sn * hipVel.x() + cs * hipVel.y(), hipVel.z());
        in.hipRadius = hipR;
        in.wheelSpeed = omega;
        in.dt = dt;

        // Bước 2 + 3: va chạm, biến dạng và lực (trong mô hình CEM / CSEM)
        double computeStart = clock.getCurrentTimeSeconds();
        StepOutput res = g_model->step(in);
        lastComputeMs = (clock.getCurrentTimeSeconds() - computeStart) * 1000.0;

        // lực về lại hệ thế giới (xoay +phi)
        cVector3d fRaw(cs * res.force.x() - sn * res.force.y(),
                       sn * res.force.x() + cs * res.force.y(),
                       res.force.z());

        // Bước 4: lọc lực (CORE 2, [2]) và giới hạn an toàn
        cVector3d fOut = fRaw;
        if (g_useFilter) fOut = filter.filter(fRaw, FILTER_CUTOFF_HZ, dt);
        else             filter.reset();

        double mag = fOut.length();
        if (mag > MAX_FORCE) fOut = (MAX_FORCE / mag) * fOut;

        // gửi cho luồng đồ hoạ
        g_hipWorld = hip;
        g_phi = phi;
        g_force = fOut;
        g_contacts = res.numContacts;
        g_avgDepth = res.avgDepth;

        // Ghi log / kết thúc thử nghiệm tự động
        bool scripted = (g_script != SCRIPT_NONE);
        if (g_script == SCRIPT_TEST)
        {
            LogRow row;
            row.t = g_scriptTime;
            row.n = res.numContacts;
            row.depth = res.avgDepth;
            row.fRaw = fRaw.length();
            row.fFilt = fOut.length();
            g_testLog.push_back(row);

            g_scriptTime += dt;
            if (g_scriptTime >= TEST_DURATION)
            {
                g_scriptDone = SCRIPT_TEST;
                g_script = SCRIPT_NONE;
            }
        }
        else if (g_script == SCRIPT_BENCH)
        {
            g_scriptTime += dt;
            if (g_scriptTime >= BENCH_DURATION)
            {
                g_scriptDone = SCRIPT_BENCH;
                g_script = SCRIPT_NONE;
            }
        }

        g_lock.release();
        lastStepMs = (clock.getCurrentTimeSeconds() - stepStart) * 1000.0;

        // Gửi lực về thiết bị (khi chạy tự động thì không đẩy tay người dùng)
        if (useDevice)
        {
            if (scripted) hapticDevice->setForce(cVector3d(0, 0, 0));
            else          hapticDevice->setForce(fOut);
        }

        freqCounterHaptics.signal(1);

        // không có thiết bị: giữ nhịp ~1 kHz như Falcon
        if (!useDevice && VIRTUAL_LOOP_HZ > 0.0)
        {
            double period = 1.0 / VIRTUAL_LOOP_HZ;
            while (clock.getCurrentTimeSeconds() - loopStart < period) {}
        }

        lastLoopMs = (clock.getCurrentTimeSeconds() - loopStart) * 1000.0;
    }

    if (hasDevice) hapticDevice->setForce(cVector3d(0, 0, 0));
    simulationFinished = true;
}

// Thử nghiệm tự động
void requestScript(ScriptType type)
{
    g_lock.acquire();
    bool busy = (g_script != SCRIPT_NONE || g_scriptRequest != SCRIPT_NONE);
    if (!busy) g_scriptRequest = type;
    g_lock.release();

    if (busy) return;
    if (type == SCRIPT_BENCH)
    {
        graphicsCopy.reset();
        graphicsMesh.reset();
    }
    statusText = "";
}

// chạy việc tiếp theo trong hàng đợi khi đang rảnh
void startNextJob()
{
    if (jobQueue.empty() || screenshotCountdown >= 0) return;

    g_lock.acquire();
    bool busy = (g_script != SCRIPT_NONE || g_scriptRequest != SCRIPT_NONE || g_scriptDone != SCRIPT_NONE);
    g_lock.release();
    if (busy) return;

    Job job = jobQueue.front();
    jobQueue.pop_front();

    if (job.modelType != g_modelType || job.preset != g_preset)
    {
        switchModel(job.modelType, job.preset);
    }
    requestScript(job.script);
}

// CEM + CSEM gốc + CSEM cải tiến ở mọi độ phân giải
void queueFullSuite(bool withTests)
{
    if (withTests)
    {
        jobQueue.push_back({ 1, g_preset, SCRIPT_TEST });
        jobQueue.push_back({ 2, CSEM_DEFAULT_PRESET, SCRIPT_TEST });
        jobQueue.push_back({ 3, CSEM_DEFAULT_PRESET, SCRIPT_TEST });
    }
    jobQueue.push_back({ 1, g_preset, SCRIPT_BENCH });
    for (int p = 0; p < CSEM_NUM_PRESETS; p++)
    {
        jobQueue.push_back({ 2, p, SCRIPT_BENCH });
        jobQueue.push_back({ 3, p, SCRIPT_BENCH });
    }
}

void handleScriptDone(ScriptType type)
{
    string tag = modelTag(g_modelType);
    string res = cStr(meshRows) + "x" + cStr(meshCols);

    if (type == SCRIPT_TEST)
    {
        // Mục 5.1: log mỗi vòng haptic -> vẽ bằng plot_force.py
        g_lock.acquire();
        vector<LogRow> log = g_testLog;
        g_lock.release();

        string filename = "force_log_" + tag + ".csv";
        ofstream file(filename);
        file << "time_ms,num_colliding,avg_depth_mm,force_raw_N,force_filtered_N\n";
        for (const LogRow& r : log)
        {
            file << r.t * 1000.0 << "," << r.n << "," << r.depth * 1000.0 << ","
                 << r.fRaw << "," << r.fFilt << "\n";
        }
        file.close();

        statusText = "Saved " + filename + " (" + cStr((int)log.size()) + " rows)";
        cout << "> " << statusText << endl;
    }
    else if (type == SCRIPT_BENCH)
    {
        // Mục 5.2: một dòng trong benchmark.csv + ảnh chụp gốm sau thao tác
        g_lock.acquire();
        Stats compute = g_benchCompute;
        Stats step = g_benchStep;
        Stats loop = g_benchLoop;
        g_lock.release();

        // độ "sắc" của bề mặt = sai phân bậc 2 của bán kính (bỏ 20 hàng sát miệng/đáy)
        double sumH = 0, sumT = 0, maxH = 0;
        long n = 0;
        for (int i = 20; i < meshRows - 20; i++)
        {
            for (int j = 0; j < meshCols; j++)
            {
                double r  = renderRadii[i * meshCols + j];
                double dH = renderRadii[(i + 1) * meshCols + j] - 2 * r + renderRadii[(i - 1) * meshCols + j];
                double dT = renderRadii[i * meshCols + (j + 1) % meshCols] - 2 * r +
                            renderRadii[i * meshCols + (j - 1 + meshCols) % meshCols];
                sumH += dH * dH;
                sumT += dT * dT;
                maxH = max(maxH, fabs(dH));
                n++;
            }
        }
        double sharpRms  = n > 0 ? sqrt(sumH / n) * 1e6 : 0.0;   // [um]
        double sharpMax  = maxH * 1e6;
        double sharpTRms = n > 0 ? sqrt(sumT / n) * 1e6 : 0.0;

        string filename = "benchmark.csv";
        bool exists = ifstream(filename).good();
        ofstream file(filename, ios::app);
        if (!exists)
        {
            file << "model,rows,cols,elements,"
                 << "compute_avg_ms,compute_min_ms,compute_max_ms,"
                 << "step_avg_ms,step_max_ms,"
                 << "haptic_hz_avg,haptic_hz_min,"
                 << "gfx_lock_avg_ms,gfx_lock_max_ms,gfx_mesh_avg_ms,"
                 << "sharp_height_rms_um,sharp_height_max_um,sharp_angle_rms_um\n";
        }
        double hzAvg = loop.avg() > 0 ? 1000.0 / loop.avg() : 0.0;
        double hzMin = loop.maxV > 0 ? 1000.0 / loop.maxV : 0.0;
        file << tag << "," << meshRows << "," << meshCols << "," << meshRows * meshCols << ","
             << compute.avg() << "," << compute.minOr0() << "," << compute.maxV << ","
             << step.avg() << "," << step.maxV << ","
             << hzAvg << "," << hzMin << ","
             << graphicsCopy.avg() << "," << graphicsCopy.maxV << "," << graphicsMesh.avg() << ","
             << sharpRms << "," << sharpMax << "," << sharpTRms << "\n";
        file.close();

        statusText = "Bench " + tag + " " + res + ": compute avg " + cStr(compute.avg(), 3) +
                     " ms, max " + cStr(compute.maxV, 3) + " ms -> benchmark.csv";
        cout << "> " << statusText << endl;

        screenshotName = "bench_" + tag + "_" + res + ".png";
        screenshotCountdown = 3;
    }
}

void saveScreenshot(const string& filename)
{
    cImagePtr image = cImage::create();
    camera->copyImageBuffer(image);
    if (image->saveToFile(filename)) cout << "> Saved " << filename << endl;
    else                            cout << "> Could not save " << filename << endl;
}

// Bàn phím
void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods)
{
    if ((a_action != GLFW_PRESS) && (a_action != GLFW_REPEAT)) return;

    if ((a_key == GLFW_KEY_ESCAPE) || (a_key == GLFW_KEY_Q))
    {
        glfwSetWindowShouldClose(a_window, GLFW_TRUE);
    }

    // đổi mô hình
    else if (a_key == GLFW_KEY_1) switchModel(1, g_preset);
    else if (a_key == GLFW_KEY_2) switchModel(2, g_preset);
    else if (a_key == GLFW_KEY_3) switchModel(3, g_preset);

    // đổi độ phân giải CSEM
    else if (a_key == GLFW_KEY_N)
    {
        int preset = (g_preset + 1) % CSEM_NUM_PRESETS;
        switchModel(g_modelType, preset);
    }

    // reset gốm
    else if (a_key == GLFW_KEY_R)
    {
        g_lock.acquire();
        g_model->reset();
        g_lock.release();
    }

    // bàn xoay
    else if (a_key == GLFW_KEY_SPACE)
    {
        g_lock.acquire();
        g_wheelOn = !g_wheelOn;
        g_lock.release();
    }
    else if (a_key == GLFW_KEY_W || a_key == GLFW_KEY_S)
    {
        g_lock.acquire();
        double step = (a_key == GLFW_KEY_W) ? WHEEL_SPEED_STEP : -WHEEL_SPEED_STEP;
        g_wheelSpeed = cClamp(g_wheelSpeed + step, -WHEEL_SPEED_MAX, WHEEL_SPEED_MAX);
        g_lock.release();
    }

    // kích thước HIP
    else if (a_key == GLFW_KEY_LEFT_BRACKET || a_key == GLFW_KEY_RIGHT_BRACKET)
    {
        g_lock.acquire();
        double step = (a_key == GLFW_KEY_RIGHT_BRACKET) ? HIP_RADIUS_STEP : -HIP_RADIUS_STEP;
        g_hipRadius = cClamp(g_hipRadius + step, HIP_RADIUS_MIN, HIP_RADIUS_MAX);
        g_lock.release();
    }

    // bộ lọc lực
    else if (a_key == GLFW_KEY_L)
    {
        g_lock.acquire();
        g_useFilter = !g_useFilter;
        g_lock.release();
    }

    // thiết bị thật <-> HIP ảo
    else if (a_key == GLFW_KEY_V)
    {
        g_lock.acquire();
        if (hasDevice) g_useDevice = !g_useDevice;
        g_lock.release();
    }

    // thử nghiệm tự động
    else if (a_key == GLFW_KEY_T) requestScript(SCRIPT_TEST);
    else if (a_key == GLFW_KEY_B)
    {
        if (a_mods & GLFW_MOD_SHIFT) queueFullSuite(false);
        else                         requestScript(SCRIPT_BENCH);
    }

    // chụp màn hình
    else if (a_key == GLFW_KEY_P)
    {
        screenshotName = "screenshot_" + modelTag(g_modelType) + "_" + cStr((int)time(NULL)) + ".png";
        screenshotCountdown = 0;
    }

    // toàn màn hình
    else if (a_key == GLFW_KEY_F)
    {
        fullscreen = !fullscreen;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (fullscreen)
        {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            int w = 0.8 * mode->height;
            int h = 0.6 * mode->height;
            int x = 0.5 * (mode->width - w);
            int y = 0.5 * (mode->height - h);
            glfwSetWindowMonitor(window, NULL, x, y, w, h, mode->refreshRate);
        }
        glfwSwapInterval(swapInterval);
    }

    else if (a_key == GLFW_KEY_M)
    {
        mirroredDisplay = !mirroredDisplay;
        camera->setMirrorVertical(mirroredDisplay);
    }
}

// Callback cửa sổ và đóng chương trình (như 01-mydevice)
void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height)
{
    windowW = a_width;
    windowH = a_height;
    renderGraphics();
}

void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height)
{
    framebufferW = a_width;
    framebufferH = a_height;
}

void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale)
{
    viewport->setContentScale(a_xscale, a_yscale);
}

void onErrorCallback(int a_error, const char* a_description)
{
    cout << "Error: " << a_description << endl;
}

void close(void)
{
    simulationRunning = false;
    while (!simulationFinished) { cSleepMs(100); }

    if (hasDevice) hapticDevice->close();

    delete hapticsThread;
    delete world;
    delete handler;
    delete g_model;
}
