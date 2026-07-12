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
    b2 = 0.0f;
    inputDim = 0;
    hiddenDim = 0;

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
    size_t outputDim = 0;
    while (in >> key)
    {
        if (key == "input_dim")
            in >> inputDim;
        else if (key == "hidden_dim")
            in >> hiddenDim;
        else if (key == "output_dim")
            in >> outputDim;
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
            w2.resize(hiddenDim);
            for (size_t i = 0; i < w2.size(); ++i)
                in >> w2[i];
        }
        else if (key == "b2")
            in >> b2;
        else
        {
            LOG_ERROR("playerbots", "MlMlpModel: unknown key {} in {}", key, path);
            return false;
        }
    }

    if (!inputDim || !hiddenDim || w1.size() != hiddenDim * inputDim || b1.size() != hiddenDim ||
        w2.size() != hiddenDim)
    {
        LOG_ERROR("playerbots", "MlMlpModel: incomplete weights in {}", path);
        return false;
    }

    loaded = true;
    LOG_INFO("playerbots", "MlMlpModel: loaded {} (in={}, hidden={})", path, inputDim, hiddenDim);
    return true;
}

float MlMlpModel::Forward(float const* input, size_t inputLen) const
{
    if (!loaded || !input || inputLen != inputDim)
        return 0.0f;

    std::vector<float> hidden(hiddenDim, 0.0f);
    for (size_t h = 0; h < hiddenDim; ++h)
    {
        float sum = b1[h];
        float const* row = &w1[h * inputDim];
        for (size_t i = 0; i < inputDim; ++i)
            sum += row[i] * input[i];
        hidden[h] = sum > 0.0f ? sum : 0.0f;  // ReLU
    }

    float out = b2;
    for (size_t h = 0; h < hiddenDim; ++h)
        out += w2[h] * hidden[h];
    return out;
}
