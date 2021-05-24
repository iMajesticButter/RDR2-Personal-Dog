#include "Main.h"

Prompt* createAccompanyPrompt()
{
	return new Prompt(DataFiles::Lang->get("interaction.accompany.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_WHISTLE"));
}

Prompt* createStayPrompt()
{
	return new Prompt(DataFiles::Lang->get("interaction.stay.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_CONTEXT_X"));
}

Prompt* createFollowPrompt()
{
	return new Prompt(DataFiles::Lang->get("interaction.follow.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_CONTEXT_X"));
}

Prompt* createFeedPrompt()
{
	Prompt* prompt = new Prompt(DataFiles::Lang->get("interaction.feed.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_INTERACT_OPTION1"));

	return prompt;
}

Prompt* createPraisePrompt()
{
	return new Prompt(DataFiles::Lang->get("interaction.praise.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_INTERACT_OPTION1"));
}

Prompt* createDismissPrompt()
{
	return new Prompt(DataFiles::Lang->get("interaction.dismiss.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_INTERACT_OPTION2"), PromptMode::SemiHold);
}

Prompt* createRenamePrompt()
{
	return new Prompt(DataFiles::Lang->get("interaction.rename.promptLabel"), GAMEPLAY::GET_HASH_KEY("INPUT_LOOK_BEHIND"), PromptMode::SemiHold);
}

Prompt* createAttackPreyPrompt(const char* dogName)
{
	return new Prompt(
		string(DataFiles::Lang->get("interaction.attack_prey.promptLabel")).append(" ").append(dogName).c_str(), 
		GAMEPLAY::GET_HASH_KEY("INPUT_INTERACT_OPTION1"), 
		PromptMode::SemiHold);
}

Prompt* createRetrieveDogPrompt(const char* dogName)
{
	return new Prompt(
		string(DataFiles::Lang->get("interaction.retrieve_dog.promptLabel")).append(" ").append(dogName).c_str(),
		GAMEPLAY::GET_HASH_KEY("INPUT_LOOK_BEHIND"),
		PromptMode::SemiHold);
}

CompanionEngine::CompanionEngine()
{
	this->state = new RootState();
	this->state->coresUI = new ComapnionCoresUI(NULL);
}

RootState* CompanionEngine::getState()
{
	return this->state;
}

void CompanionEngine::start()
{
	if (!strlen(DataFiles::Dog->get("model")))
	{
		return;
	}

	vector<Ped> nearbyPeds = getNearbyPeds(player, 20);
	Ped dog = NULL;
	for (Ped current : nearbyPeds)
	{
		if (!ENTITY::IS_ENTITY_DEAD(current) && DECORATOR::DECOR_GET_INT(current, "SH_CMP_companion") == 1)
		{
			dog = current;
			break;
		}
	}
	if (!dog)
	{
		dog = spawnDog();
	}

	accompanyDog(dog);
	const char* name = DataFiles::Dog->get("name");
	if (name && strlen(name))
	{
		state->companionApi->setName(name);
	}
}

Ped CompanionEngine::spawnDog()
{
	Ped dog = createPed(DataFiles::Dog->getInt("model"), *getSafeCoordForPed(playerPos() + getForwardVector(player)));
	PED::_0x77FF8D35EEC6BBC4(dog, DataFiles::Dog->getInt("preset_index"), 0); // _SET_PED_OUTFIT_PRESET
	return dog;
}

void CompanionEngine::update()
{
	int n = SCRIPT::GET_NUMBER_OF_EVENTS(0);
	for (int i = 0; i < n; i++)
	{
		int eventType = SCRIPT::GET_EVENT_AT_INDEX(0, i);
		if (eventType == GAMEPLAY::GET_HASH_KEY("EVENT_PED_ANIMAL_INTERACTION"))
		{
			onAnimalInteraction(i);
		}
		else if (eventType == GAMEPLAY::GET_HASH_KEY("EVENT_SHOT_FIRED_BULLET_IMPACT"))
		{
			onBulletImpact(i);
		}
		else if (eventType == GAMEPLAY::GET_HASH_KEY("EVENT_PLAYER_PROMPT_TRIGGERED"))
		{
			onPromptTriggered(i);
		}
	}

	if (state->candidateDog && state->accompanyPrompt && state->accompanyPrompt->isActivatedByPlayer())
	{
		bondWithDog();
	}

	if (!state->companionDog || !state->companionApi)
	{
		return;
	}

	if (!ENTITY::DOES_ENTITY_EXIST(state->companionDog))
	{
		deleteBlipSafe(&state->companionBlip);
		accompanyDog(spawnDog());
		return;
	}

	if (ENTITY::IS_ENTITY_DEAD(state->companionDog))
	{
		onCompnaionDied();
		return;
	}

	state->isWithinWhistlingRange = distance(player, state->companionDog) < DataFiles::Dog->getInt("whistling_range");

	if (!PED::IS_PED_GROUP_MEMBER(state->companionDog, PED::GET_PED_GROUP_INDEX(player), 0))
	{
		PED::SET_PED_AS_GROUP_MEMBER(state->companionDog, PED::GET_PED_GROUP_INDEX(player));
	}

	if (CONTROLS::IS_CONTROL_JUST_RELEASED(0, GAMEPLAY::GET_HASH_KEY("INPUT_WHISTLE")))
	{
		onWhistle(0);
	}

	float companionPreviousHealthRate = state->previousCompanionHealth / ENTITY::GET_ENTITY_MAX_HEALTH(state->companionDog, 1);
	float companionCurrentHealthRate = state->companionApi->getHealthRate();
	if (companionPreviousHealthRate > companionCurrentHealthRate &&
		companionPreviousHealthRate > DataFiles::DogMeta->getFloat("low_health_threshold") &&
		companionCurrentHealthRate < DataFiles::DogMeta->getFloat("low_health_threshold"))
	{
		showHelpText(DataFiles::Lang->get("tutorial.companion_low_health"));
	}

	if (state->currentTask)
	{
		if (state->currentTask->isRunning())
		{
			state->currentTask->update();
		}
		else if (state->currentTask->isDone())
		{
			state->currentTask = state->currentTask->getChainedTask();
		}
		else
		{
			state->currentTask = NULL;
		}
	}

	Ped playerTarget = PED::_0xCD66FEA29400A0B5(player); // GET_CURRENT_TARGET_FOR_PED
	if (playerTarget)
	{
		state->companionApi->combat(playerTarget);
	}

	state->previousCompanionHealth = (float)ENTITY::GET_ENTITY_HEALTH(state->companionDog);

	scanCompanionSurrounding();
	updatePrompts();
	updateGUI();
}

void CompanionEngine::scanCompanionSurrounding()
{
	if (SYSTEM::TIMERA() <= DataFiles::DogMeta->getInt("scan_surrounding_interval") ||
		!state->isWithinWhistlingRange)
	{
		return;
	}
	SYSTEM::SETTIMERA(0);

	if (!state->calmTimer.isStarted() || state->calmTimer.getElapsedSeconds() >= DataFiles::DogMeta->getInt("calmTimeout"))
	{
		state->calmTimer.stop();
		Vector3 companionCoords = entityPos(state->companionDog);
		float detectionRadius = DataFiles::DogMeta->getInt("ambient_dangers_detection_radius");
		Ped predator = findClosestPredatorAround(player, detectionRadius);
		if (predator)
		{
			onPredatorDetected(predator);
		}
	}

	if (INTERIOR::GET_INTERIOR_FROM_ENTITY(player) && !state->companionApi->isSittingDown())
	{
		state->currentTask = state->companionApi->waitOutsideInterior();
		tutorial("companion_wait_outside");
	}
}

void CompanionEngine::updatePrompts()
{
	if (state->stayPrompt->isActivatedByPlayer())
	{
		CompanionCommands::commandStay(state);
	}

	if (state->followPrompt->isActivatedByPlayer())
	{
		CompanionCommands::commandFollow(state);
	}

	if (state->praisePrompt->isActivatedByPlayer())
	{
		CompanionCommands::commandPraise(state);
	}

	if (distance(player, state->companionDog) < 2)
	{
		if (PED::_0xA911EE21EDF69DAF(player) && // IS_PED_CARRYING_SOMETHING
			state->companionApi->isEntityEatable(findCarriedPedBy(player)))
		{
			if (!state->companionApi->isBegging())
			{
				state->companionApi->beg(12000, findCarriedPedBy(player));
			}
			state->feedPrompt->show();
			state->praisePrompt->hide();
			if (state->feedPrompt->isActivatedByPlayer())
			{
				state->companionApi->getPreyMeal(player);
			}

			tutorial("companion_meal");
		}
		else
		{
			state->praisePrompt->show();
			state->feedPrompt->hide();
		}

		state->renamePrompt->show();
		if (state->renamePrompt->isActivatedByPlayer())
		{
			renameCompanion(getKeyboardInput("Enter Name", "Enter your dog's new name", "Dog", 20));
		}

		state->dismissPrompt->show();
		if (state->dismissPrompt->isActivatedByPlayer())
		{
			dismissCompanion();
		}
	}
	else
	{
		state->dismissPrompt->hide();
		state->feedPrompt->hide();
		state->renamePrompt->hide();
	}

	if (state->companionApi->isSittingDown())
	{
		state->followPrompt->show();
		state->stayPrompt->hide();
	}
	else
	{
		state->followPrompt->hide();
		state->stayPrompt->show();
	}

	Entity targetEntity;
	if (ScriptSettings::getBool("AllowHuntingPrompt") &&
		PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(PLAYER::PLAYER_ID(), &targetEntity) && 
		targetEntity != state->companionDog &&
		!PED::_0x772A1969F649E902(ENTITY::GET_ENTITY_MODEL(targetEntity)) && // _IS_THIS_MODEL_A_HORSE
		ENTITY::_0x9A100F1CF4546629(targetEntity) && // GET_IS_ANIMAL
		!ENTITY::IS_ENTITY_DEAD(targetEntity) &&
		distanceBetweenEntities(player, targetEntity) < DataFiles::Dog->getInt("whistling_range") * DataFiles::DogMeta->getFloat("attack_prey_threshold")) 
	{
		if (!state->attackPreyPrompt || state->attackPreyPrompt->getTargetEntity() != targetEntity)
		{
			state->attackPreyPrompt = createAttackPreyPrompt(DataFiles::Dog->get("name"));	
		}
		state->attackPreyPrompt->setTargetEntity(targetEntity);
		state->attackPreyPrompt->show();
		if (state->attackPreyPrompt->isActivatedByPlayer())
		{
			state->companionApi->hunt(targetEntity, 12000);
		}
	}
	else if (state->attackPreyPrompt)
	{
		state->attackPreyPrompt->hide();
	}

	if (!state->isWithinWhistlingRange)
	{
		state->retrieveDogPrompt->setTargetEntity(getPlayerSaddleHorse());
		state->retrieveDogPrompt->show();
		if (state->retrieveDogPrompt->isActivatedByPlayer())
		{
			CompanionCommands::commandRetrieve(state);
		}
	}
	else
	{
		state->retrieveDogPrompt->hide();
	}

	if (AUDIO::IS_ANY_SPEECH_PLAYING(player))
	{
		state->praisePrompt->setIsEnabled(false);
		state->stayPrompt->setIsEnabled(false);
		state->dismissPrompt->setIsEnabled(false);
		state->followPrompt->setIsEnabled(false);
	}
	else
	{
		state->praisePrompt->setIsEnabled(true);
		state->stayPrompt->setIsEnabled(true);
		state->dismissPrompt->setIsEnabled(true);
		state->followPrompt->setIsEnabled(true);
	}

	state->stayPrompt->setIsEnabled(!state->companionApi->isAgitated());
}

void CompanionEngine::updateGUI()
{
	if (!ScriptSettings::getBool("ShowCores"))
	{
		return;
	}

	if (state->companionApi->isAgitated())
	{
		state->coresUI->setCoreModifier(CompanionCoreModifiers::Agitated);
	}
	else
	{
		state->coresUI->setCoreModifier(CompanionCoreModifiers::None);
	}


	if (getPlayerTargetEntity() == state->companionDog || state->companionApi->isAgitated())
	{	
		state->coresUI->show();
		UI::_0xD4EE21B7CC7FD350(true); // _SHOW_HORSE_CORES
	}
	else
	{
		state->coresUI->hide();
		UI::_0xD4EE21B7CC7FD350(false); // _SHOW_HORSE_CORES
	}

	state->coresUI->update();
}

void CompanionEngine::onAnimalInteraction(int eventIndex)
{
	if (PED::IS_PED_ON_MOUNT(player) || state->companionDog)
	{
		return;
	}

	int arr[10];
	if (!SCRIPT::GET_EVENT_DATA(0, eventIndex, arr, 3))
	{
		return;
	}

	int interactionType = arr[1];
	int animalPedId = arr[2];
	if (arr[0] == player && interactionType == 32759 && isPedADog(animalPedId))
	{
		state->candidateDog = animalPedId;
		if (state->accompanyPrompt)
		{
			state->accompanyPrompt->hide();
		}
		state->accompanyPrompt = createAccompanyPrompt();
		state->accompanyPrompt->setTargetEntity(state->candidateDog);
		state->accompanyPrompt->show();
		state->accompanyPrompt->setIsEnabled(true);
		tutorial("bond_2");
	}

}

void CompanionEngine::onPromptTriggered(int eventIndex)
{
	if (!state->companionDog && !state->companionApi->isSittingDown())
	{
		return;
	}

	int eventData[30];
	if (!SCRIPT::GET_EVENT_DATA(0, eventIndex, eventData, 10))
	{
		return;
	}

	if (!eventData[0] == 34) // PP_TRACK_ANIMAL
	{
		return;
	}

	Ped trackedEntity = eventData[4];
	Vector3 pos = entityPos(trackedEntity);
	state->currentTask = state->companionApi->track(trackedEntity);
}

void CompanionEngine::bondWithDog()
{
	log("animal bonded");
	state->accompanyPrompt->hide();
	DataFiles::Dog->set("preset_index", 1);
	DataFiles::Dog->set("bonding_level", 1);
	DataFiles::Dog->set("max_health", DataFiles::DogMeta->getInt("bonding.1.max_health"));
	DataFiles::Dog->set("will_attack_predators", 1);
	DataFiles::Dog->set("name", DataFiles::Lang->get("companion.defaultBlipName"));
	DataFiles::Dog->set("whistling_range", DataFiles::DogMeta->getInt("bonding.1.whistling_range"));
	DataFiles::Dog->set("fetch_range_min", 20);
	DataFiles::Dog->set("fetch_range_max", 70);
	accompanyDog(state->candidateDog);
	state->candidateDog = NULL;
	DataFiles::Dog->set("model", (int )ENTITY::GET_ENTITY_MODEL(state->companionDog));
	DataFiles::Dog->save();
}

void CompanionEngine::accompanyDog(Ped dog)
{
	int group = PED::GET_PED_GROUP_INDEX(player);
	if (!PED::DOES_GROUP_EXIST(group))
	{
		group = PED::CREATE_GROUP(0);
		PED::SET_PED_AS_GROUP_LEADER(player, group, 1);
	}

	state->companionDog = dog;
	state->companionApi = new DogCompanionInterface(dog);

	ENTITY::SET_ENTITY_AS_MISSION_ENTITY(dog, 1, 0);
	PED::SET_PED_AS_GROUP_MEMBER(dog, group);
	PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(dog, PLAYER::PLAYER_ID(), false);
	PED::SET_PED_RELATIONSHIP_GROUP_HASH(dog, PED::GET_PED_RELATIONSHIP_GROUP_HASH(player));
	DECORATOR::DECOR_SET_INT(dog, "SH_CMP_companion", 1);

	PED::SET_PED_CONFIG_FLAG(dog, 154, true); // PCF_UseFollowLeaderThreatResponse

	ANIMALTUNING::_0x9FF1E042FA597187(dog, 44, 0); // ATB_StartFleeDecisionMakerDuringAlertedState
	ANIMALTUNING::_0x9FF1E042FA597187(dog, 32, 1); // ATB_NeverRetreatFromMelee

	ANIMALTUNING::_0xCBDA22C87977244F(dog, 74, 1); // ATF_ThreatResponseCombatProbability
	ANIMALTUNING::_0xCBDA22C87977244F(dog, 80, 1); // ATF_ThreatResponseLoudNoiseFleeOrCombatRange
	ANIMALTUNING::_0xCBDA22C87977244F(dog, 115, 1); // ATF_ThreatResponseNoiseSmallCaliberFleeOrCombatRange
	ANIMALTUNING::_0xCBDA22C87977244F(dog, 117, 1); // ATF_ThreatResponseNoiseMediumCaliberFleeOrCombatRange
	ANIMALTUNING::_0xCBDA22C87977244F(dog, 119, 1); // ATF_ThreatResponseNoiseBigCaliberFleeOrCombatRange
	ANIMALTUNING::_0xCBDA22C87977244F(dog, 165, 5);
	ENTITY::SET_ENTITY_MAX_HEALTH(dog, DataFiles::Dog->getInt("max_health"));
	ENTITY::SET_ENTITY_HEALTH(dog, ENTITY::GET_ENTITY_MAX_HEALTH(dog, 0), 0);

	Blip dogBlip = createBlip(dog, 0x19365607);
	state->companionBlip = dogBlip;
	setBlipLabel(dogBlip, DataFiles::Lang->get("companion.defaultBlipName"));
	RADAR::_0x662D364ABF16DE2F(dogBlip, 0xB93E613);

	PED::_0x01B21B81865E2A1F(dog, 0); // _SET_PED_SCENT

	SYSTEM::SETTIMERA(0);

	state->stayPrompt = createStayPrompt();
	state->stayPrompt->setTargetEntity(state->companionDog);
	state->stayPrompt->show();

	state->praisePrompt = createPraisePrompt();
	state->praisePrompt->setTargetEntity(state->companionDog);
	state->praisePrompt->show();

	state->feedPrompt = createFeedPrompt();
	state->feedPrompt->setTargetEntity(state->companionDog);
	state->feedPrompt->hide();

	state->dismissPrompt = createDismissPrompt();
	state->dismissPrompt->setTargetEntity(state->companionDog);
	state->dismissPrompt->hide();

	state->followPrompt = createFollowPrompt();
	state->followPrompt->setTargetEntity(state->companionDog);
	state->followPrompt->hide();

	state->renamePrompt = createRenamePrompt();
	state->renamePrompt->setTargetEntity(state->companionDog);
	state->renamePrompt->hide();
	
	state->retrieveDogPrompt = createRetrieveDogPrompt(DataFiles::Dog->get("name"));
	state->retrieveDogPrompt->hide();

	state->previousCompanionHealth = (float)ENTITY::GET_ENTITY_HEALTH(state->companionDog);
	this->state->coresUI->setPed(dog);
}

void CompanionEngine::onBulletImpact(int eventIndex)
{
	if (!state->companionDog)
	{
		return;
	}

	if (state->companionApi && 
		state->companionApi->isPerformingScenario() && 
		state->isWithinWhistlingRange
	{
		return;
	}

	int arr[1];
	if (!SCRIPT::GET_EVENT_DATA(0, eventIndex, arr, 1))
	{
		return;
	}
	
	int entityId = arr[0];
	if (ENTITY::IS_ENTITY_DEAD(entityId) &&
		ENTITY::_0x9A100F1CF4546629(entityId) /* IS_PED_ANIMAL */ &&
		!PED::_0x772A1969F649E902(ENTITY::GET_ENTITY_MODEL(entityId) /*_IS_THIS_MODEL_A_HORSE*/) &&
		!ENTITY::_0x5594AFE9DE0C01B7(entityId) && // _GET_IS_PREDATOR
		PED::GET_PED_SOURCE_OF_DEATH(entityId) == player)
	{
		float distanceToAnimal = distance(player, entityId);
		if (distanceToAnimal < DataFiles::Dog->getInt("fetch_range_min") || distanceToAnimal > DataFiles::Dog->getInt("fetch_range_max"))
		{
			return;
		}

		if (ScriptSettings::getBool("MarkHuntedAnimals"))
		{
			state->companionApi->fetch(entityId);
			tutorial("fetch");
		}
	}
}

void CompanionEngine::onWhistle(int eventIndex)
{
	if (distance(player, state->companionDog) < DataFiles::Dog->getInt("whistling_range"))
	{
		if (INTERIOR::GET_INTERIOR_FROM_ENTITY(player))
		{
			return;
		}

		CompanionCommands::triggerFollow(this->state);
	}
	else
	{
		showHelpText(DataFiles::Lang->get("tutorial.companion_out_of_whistling_range"));
	}
}

void CompanionEngine::onPredatorDetected(Ped predator)
{
	float predatorDistanceToPlayer = distance(predator, player);
	float predatorDistanceToCompanion = distance(state->companionDog, predator);

	if (isPedASnake(predator))
	{
		if (predatorDistanceToCompanion > DataFiles::DogMeta->getInt("snakes_detection_radius"))
		{
			return;
		}

		float snakesAttackDistance = DataFiles::DogMeta->getInt("attack_snakes_distace");
		if (predatorDistanceToCompanion < snakesAttackDistance ||
			predatorDistanceToPlayer < snakesAttackDistance)
		{
			state->companionApi->combat(predator);
		}
		else if (!state->companionApi->isAgitated())
		{
			state->currentTask = state->companionApi->warnSnake(predator, 6000);
		}
	}
	else
	{
		int attackDistance = DataFiles::DogMeta->getInt("attack_predators_distance");
		if (DataFiles::Dog->getBool("will_attack_predators") && 
			predatorDistanceToPlayer <= attackDistance && 
			!state->companionApi->isInCombat())
		{
			state->companionApi->combat(predator);
		}
		else if (!state->companionApi->isAgitated())
		{
			state->currentTask = state->companionApi->warn(predator, 10000);
		}
	}

	tutorial("predator_detection");
}

void CompanionEngine::onCompnaionDied()
{
	showHelpText(DataFiles::Lang->get("tutorial.companion_died"));
	log("companion died");
	clearCompanion();
}

void CompanionEngine::dismissCompanion()
{
	log("companion dismissed");
	state->companionApi->dismiss();
	AI::_0xCD181A959CFDD7F4(player, state->companionDog, GAMEPLAY::GET_HASH_KEY("Interaction_Dog_Patting"), 0, 0);
	PED::REMOVE_PED_FROM_GROUP(state->companionDog);
	PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(state->companionDog, PLAYER::PLAYER_ID(), true);
	PED::SET_PED_RELATIONSHIP_GROUP_HASH(state->companionDog, PED::GET_PED_RELATIONSHIP_GROUP_DEFAULT_HASH(state->companionDog));
	ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&state->companionDog);
	clearCompanion();
}

void CompanionEngine::clearCompanion()
{
	DECORATOR::DECOR_SET_INT(state->companionDog, "SH_CMP_companion", 0);
	state->companionDog = NULL;
	deleteBlipSafe(&state->companionBlip);
	DataFiles::Dog->set("model", "");
	DataFiles::Dog->set("name", "");
	DataFiles::Dog->set("bonding_level", 1);
	DataFiles::Dog->set("max_health", 1);
	DataFiles::Dog->save();

	state->dismissPrompt->hide();
	state->feedPrompt->hide();
	state->praisePrompt->hide();
	state->stayPrompt->hide();
	state->renamePrompt->hide();
	state->followPrompt->hide();
	state->attackPreyPrompt->hide();

	log("companion has been cleared");
}

void CompanionEngine::renameCompanion(const char* name)
{
	if (name && strlen(name))
	{
		log(string("companion renamed to: ").append(name).c_str());
		DataFiles::Dog->set("name", name);
		state->companionApi->setName(name);
		state->companionApi->getPraised(player);
		setBlipLabel(state->companionBlip, name);
		DataFiles::Dog->save();
	}
}

void CompanionEngine::tutorial(const char* tutorialKey)
{
	if (!DataFiles::TutorialFlags->getBool(tutorialKey))
	{
		showHelpText(DataFiles::Lang->get(string("tutorial.").append(tutorialKey).c_str()));
		log(string("tutorial seen: ").append(tutorialKey));
		DataFiles::TutorialFlags->set(tutorialKey, 1);
		DataFiles::TutorialFlags->save();
	}
}