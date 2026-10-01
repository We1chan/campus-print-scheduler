#pragma once
#include "ui_theme.hpp"
#include <QHBoxLayout>

namespace cps::gui::ui {
// Shared desktop shell: navigation stays in one place while the page changes.
struct StudioShell {
    QVBoxLayout* content;
    QBoxLayout* navigation;
};
inline StudioShell studioShell(QWidget* central, const QString& role) {
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    auto* rail = new QFrame; rail->setObjectName("studioRail");
    auto* navigation = new QHBoxLayout(rail);
    navigation->setContentsMargins(28, 8, 28, 8); navigation->setSpacing(12);
    auto* mark = label(QStringLiteral("纸间"), "studioBrand"); navigation->addWidget(mark);
    navigation->addWidget(label(role, "railCaption")); navigation->addStretch();
    root->addWidget(rail);
    auto* main = new QWidget; main->setObjectName("studioContent");
    auto* content = new QVBoxLayout(main);
    content->setContentsMargins(28, 14, 28, 14); content->setSpacing(14);
    root->addWidget(main, 1);
    return {content, navigation};
}
inline void finishRail(QBoxLayout* navigation) {
    navigation->addStretch();
}
} // namespace cps::gui::ui
