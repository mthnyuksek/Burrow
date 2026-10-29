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

// The template with name, description and body in the given vault language ("tr" or "en").
// Tools and ids are the same in both; a template without an English text stays Turkish.
SkillDefinition LocalizedSkill(const SkillTemplate& t, const std::string& language);
