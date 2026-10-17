//  CEMModel.h  —  CORE 1: Cylindrical Element Method [1] Han et al. 2007
//
//  Gốm = một cột N hình trụ mỏng xếp chồng theo chiều cao.
//  Mỗi trụ chỉ có 1 bán kính -> biến dạng luôn đối xứng trụ.
#ifndef CEM_MODEL_H
#define CEM_MODEL_H

#include "PotteryModel.h"

class CEMModel : public PotteryModel
{
public:
    CEMModel();

    const char* name() const override { return "CEM (core 1)"; }
    void reset() override;
    StepOutput step(const StepInput& in) override;
    void copyRows(int row0, int row1, std::vector<double>& out) const override;

private:
    std::vector<double> m_radius;   // m_radius[i] = bán kính trụ thứ i
    double m_dh;                    // khoảng cách giữa hai trụ theo chiều cao
};

#endif
