/*
** Copyright 2007-2024, RTE (https://www.rte-france.com)
** See AUTHORS.txt
** SPDX-License-Identifier: MPL-2.0
** This file is part of Antares-Simulator,
** Adequacy and Performance assessment for interconnected energy networks.
**
** Antares_Simulator is free software: you can redistribute it and/or modify
** it under the terms of the Mozilla Public Licence 2.0 as published by
** the Mozilla Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** Antares_Simulator is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** Mozilla Public Licence 2.0 for more details.
**
** You should have received a copy of the Mozilla Public Licence 2.0
** along with Antares_Simulator. If not, see <https://opensource.org/license/mpl-2-0/>.
*/
//
// Created by marechaljas on 03/07/23.
//

#pragma once

#include "TSnumberData.h"

namespace Antares::Data::ScenarioBuilder
{
class BindingConstraintsTSNumberData: public TSNumberData
{
public:
    BindingConstraintsTSNumberData() = default;
    virtual ~BindingConstraintsTSNumberData() = default;

    bool reset(const Study& study) override;
    void saveToINIFile(const Study& study, Yuni::IO::File::Stream& file) const override;

    void setTSnumber(const std::string& group_name, unsigned year, unsigned value);
    unsigned get(const std::string& group_name, unsigned year) const;
    bool apply(Study& study) override;
    CString<512, false> get_prefix() const override;
    unsigned get_tsGenCount(const Study& study) const override;

private:
    std::map<std::string, MatrixType> rules_;
};

inline unsigned BindingConstraintsTSNumberData::get(const std::string& group_name,
                                                    const unsigned year) const
{
    auto it = rules_.find(group_name);
    if (it == rules_.end())
    {
        return 0;
    }
    return it->second[0][year];
}

inline CString<512, false> BindingConstraintsTSNumberData::get_prefix() const
{
    return "bc,";
}

inline unsigned BindingConstraintsTSNumberData::get_tsGenCount(const Study&) const
{
    return 0;
}
} // namespace Antares::Data::ScenarioBuilder
