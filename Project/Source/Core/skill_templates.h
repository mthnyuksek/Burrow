#pragma once

#include "Core/skill_definition.h"

#include <vector>

// Skill templates that are compiled into the program. Templates marked
// installed_by_default are copied into every newly created vault; all of them can be
// added later from the "Yetenek Ekle" dialog.
struct SkillTemplate {
    SkillDefinition skill;  // skill.id is the file name used when installed by default
    bool installed_by_default = false;
};

const std::vector<SkillTemplate>& SkillTemplates();
