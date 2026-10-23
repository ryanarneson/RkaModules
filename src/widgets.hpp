#pragma once
#include <rack.hpp>
#include "RkaTheme.hpp"

// Components shared by every RKA module, so the plugin keeps one look.
// Colours come from the generated RkaTheme.hpp (the panel palette).
namespace rka {

using namespace rack;

// White knobs with a dark pointer, read well on the navy panels.
struct BigKnob : componentlibrary::Rogan3PSWhite {};
struct SmallKnob : componentlibrary::Rogan1PWhite {};

// Two-colour LED: first colour for positive, second for negative.
template <typename TBase = app::ModuleLightWidget>
struct TCyanMagentaLight : componentlibrary::TGrayModuleLightWidget<TBase> {
	TCyanMagentaLight() {
		this->addBaseColor(RkaTheme::CYAN);
		this->addBaseColor(RkaTheme::MAGENTA);
	}
};
using CyanMagentaLight = TCyanMagentaLight<>;

}
