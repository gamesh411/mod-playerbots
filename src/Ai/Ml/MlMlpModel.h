/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MLMLPMODEL_H
#define PLAYERBOTS_MLMLPMODEL_H

#include <string>
#include <vector>

// Tiny 2-layer MLP: ReLU(W1 x + b1) -> W2 h + b2
// File format: text "PBML1" (see tools/ml/train_ranker.py)
class MlMlpModel
{
public:
    bool Load(std::string const& path);
    bool IsLoaded() const { return loaded; }
    size_t InputDim() const { return inputDim; }

    // Returns raw output (unbounded). Caller maps to multiplier.
    float Forward(float const* input, size_t inputLen) const;

private:
    bool loaded = false;
    size_t inputDim = 0;
    size_t hiddenDim = 0;
    std::vector<float> w1;  // hidden * input, row-major
    std::vector<float> b1;
    std::vector<float> w2;  // 1 * hidden
    float b2 = 0.0f;
};

#endif
