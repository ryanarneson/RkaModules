#include "plugin.hpp"


Plugin* pluginInstance;


void init(Plugin* p) {
	pluginInstance = p;

	// Add modules here
	p->addModel(modelSway);
	p->addModel(modelFathom);
	p->addModel(modelFog);
	p->addModel(modelHadal);
	p->addModel(modelBallast);
	p->addModel(modelPan);
	p->addModel(modelSquall);
	p->addModel(modelRigging);
	p->addModel(modelWake);
	p->addModel(modelNarrows);
	p->addModel(modelShoal);
	p->addModel(modelSluice);
	p->addModel(modelEddy);
	p->addModel(modelSpindrift);
	p->addModel(modelBeacon);
	p->addModel(modelNeap);
	p->addModel(modelSpring);

	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
