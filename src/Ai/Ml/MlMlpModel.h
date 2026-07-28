/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MLMLPMODEL_H
#define PLAYERBOTS_MLMLPMODEL_H

#include <string>
#include <unordered_map>
#include <vector>

#include "Define.h"

// Tiny 2-layer MLP: ReLU(W1 x + b1) -> W2 h + b2
// File format: text "PBML1" (see tools/ml/train_ranker.py)
// S1: output_dim 1 (scalar). S2: output_dim N + vocab spell ids (DEC-026 multi-logit).
class MlMlpModel
{
public:
    bool Load(std::string const& path);
    bool IsLoaded() const { return loaded; }
    size_t InputDim() const { return inputDim; }
    size_t OutputDim() const { return outputDim; }
    bool IsMultiLogit() const { return loaded && outputDim > 1 && !vocab.empty(); }
    std::vector<uint32> const& Vocab() const { return vocab; }

    // Scalar path (S1). Returns 0 if multi-logit.
    float Forward(float const* input, size_t inputLen) const;

    // Multi-logit path (S2). Fills out[outputDim]. Returns false if not multi-logit / bad input.
    bool ForwardMulti(float const* input, size_t inputLen, float* out, size_t outLen) const;

    // Vocab index for spell id, or -1 if absent.
    int VocabIndex(uint32 spellId) const;

private:
    bool loaded = false;
    size_t inputDim = 0;
    size_t hiddenDim = 0;
    size_t outputDim = 1;
    std::vector<uint32> vocab;
    std::unordered_map<uint32, size_t> vocabIndex;
    std::vector<float> w1;  // hidden * input, row-major
    std::vector<float> b1;
    std::vector<float> w2;  // output * hidden, row-major (S1: 1 * hidden)
    std::vector<float> b2;  // output (S1: size 1)
};

#endif
