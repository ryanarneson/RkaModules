#pragma once
#include <rack.hpp>
#include "RkaTheme.hpp"

// Components shared by every RKA module, so the plugin keeps one look.
// Colours come from the generated RkaTheme.hpp (the panel palette).
namespace rka {

using namespace rack;

// White knobs with a dark pointer, read well on the navy panels.
struct BigKnob : componentlibrary::Rogan3PSWhite {};
struct SmallKnob : componentlibrary::BefacoTinyKnob {};
// Attenuverters.
struct Trim : componentlibrary::Trimpot {};

// Two-colour LED: first colour for positive, second for negative.
template <typename TBase = app::ModuleLightWidget>
struct TCyanMagentaLight : componentlibrary::TGrayModuleLightWidget<TBase> {
	TCyanMagentaLight() {
		this->addBaseColor(RkaTheme::CYAN);
		this->addBaseColor(RkaTheme::MAGENTA);
	}
};
using CyanMagentaLight = TCyanMagentaLight<>;

template <typename TBase = app::ModuleLightWidget>
struct TAmberLight : componentlibrary::TGrayModuleLightWidget<TBase> {
	TAmberLight() {
		this->addBaseColor(RkaTheme::AMBER);
	}
};
using AmberLight = TAmberLight<>;

// Latching push button with an amber light inside (mode switches).
using AmberLatch = componentlibrary::VCVLightLatch<componentlibrary::MediumSimpleLight<AmberLight>>;

}
