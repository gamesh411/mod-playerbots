/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MlMlpModel.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include "Log.h"

bool MlMlpModel::Load(std::string const& path)
{
    loaded = false;
    w1.clear();
    b1.clear();
    w2.clear();
    b2.clear();
    vocab.clear();
    vocabIndex.clear();
    inputDim = 0;
    hiddenDim = 0;
    outputDim = 1;

    std::ifstream in(path.c_str());
    if (!in)
    {
        LOG_ERROR("playerbots", "MlMlpModel: cannot open {}", path);
        return false;
    }

    std::string magic;
    in >> magic;
    if (magic != "PBML1")
    {
        LOG_ERROR("playerbots", "MlMlpModel: bad magic in {}", path);
        return false;
    }

    std::string key;
    while (in >> key)
    {
        if (key == "input_dim")
            in >> inputDim;
        else if (key == "hidden_dim")
            in >> hiddenDim;
        else if (key == "output_dim")
            in >> outputDim;
        else if (key == "vocab")
        {
            size_t n = 0;
            in >> n;
            vocab.resize(n);
            for (size_t i = 0; i < n; ++i)
                in >> vocab[i];
        }
        else if (key == "W1")
        {
            w1.resize(hiddenDim * inputDim);
            for (size_t i = 0; i < w1.size(); ++i)
                in >> w1[i];
        }
        else if (key == "b1")
        {
            b1.resize(hiddenDim);
            for (size_t i = 0; i < b1.size(); ++i)
                in >> b1[i];
        }
        else if (key == "W2")
        {
            size_t const rows = outputDim ? outputDim : 1;
            w2.resize(rows * hiddenDim);
            for (size_t i = 0; i < w2.size(); ++i)
                in >> w2[i];
        }
        else if (key == "b2")
        {
            size_t const rows = outputDim ? outputDim : 1;
            b2.resize(rows);
            for (size_t i = 0; i < rows; ++i)
                in >> b2[i];
        }
        else
        {
            LOG_ERROR("playerbots", "MlMlpModel: unknown key {} in {}", key, path);
            return false;
        }
    }

    if (!outputDim)
        outputDim = 1;

    if (!inputDim || !hiddenDim || w1.size() != hiddenDim * inputDim || b1.size() != hiddenDim ||
        w2.size() != outputDim * hiddenDim || b2.size() != outputDim)
    {
        LOG_ERROR("playerbots", "MlMlpModel: incomplete weights in {}", path);
        return false;
    }

    if (outputDim > 1)
    {
        if (vocab.size() != outputDim)
        {
            LOG_ERROR("playerbots", "MlMlpModel: vocab size {} != output_dim {} in {}", vocab.size(), outputDim, path);
            return false;
        }
        for (size_t i = 0; i < vocab.size(); ++i)
            vocabIndex[vocab[i]] = i;
    }

    loaded = true;
    if (IsMultiLogit())
        LOG_INFO("playerbots", "MlMlpModel: loaded {} (in={}, hidden={}, out={}, vocab={})", path, inputDim, hiddenDim,
                 outputDim, vocab.size());
    else
        LOG_INFO("playerbots", "MlMlpModel: loaded {} (in={}, hidden={})", path, inputDim, hiddenDim);
    return true;
}

int MlMlpModel::VocabIndex(uint32 spellId) const
{
    auto it = vocabIndex.find(spellId);
    if (it == vocabIndex.end())
        return -1;
    return static_cast<int>(it->second);
}

float MlMlpModel::Forward(float const* input, size_t inputLen) const
{
    if (!loaded || !input || inputLen < inputDim || outputDim != 1)
        return 0.0f;

    std::vector<float> hidden(hiddenDim, 0.0f);
    for (size_t h = 0; h < hiddenDim; ++h)
    {
        float sum = b1[h];
        float const* row = &w1[h * inputDim];
        for (size_t i = 0; i < inputDim; ++i)
            sum += row[i] * input[i];
        hidden[h] = sum > 0.0f ? sum : 0.0f;
    }

    float out = b2[0];
    for (size_t h = 0; h < hiddenDim; ++h)
        out += w2[h] * hidden[h];
    return out;
}

bool MlMlpModel::ForwardMulti(float const* input, size_t inputLen, float* out, size_t outLen) const
{
    if (!IsMultiLogit() || !input || !out || inputLen < inputDim || outLen < outputDim)
        return false;

    std::vector<float> hidden(hiddenDim, 0.0f);
    for (size_t h = 0; h < hiddenDim; ++h)
    {
        float sum = b1[h];
        float const* row = &w1[h * inputDim];
        for (size_t i = 0; i < inputDim; ++i)
            sum += row[i] * input[i];
        hidden[h] = sum > 0.0f ? sum : 0.0f;
    }

    for (size_t o = 0; o < outputDim; ++o)
    {
        float sum = b2[o];
        float const* row = &w2[o * hiddenDim];
        for (size_t h = 0; h < hiddenDim; ++h)
            sum += row[h] * hidden[h];
        out[o] = sum;
    }
    return true;
}
