/*
 * Copyright 2025, Kintsugi OS Contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2015, Haiku.
 * Authors:
 *		Julian Harnath <julian.harnath@rwth-aachen.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file PictureBoundingBoxPlayer.h
 *  @brief Computes the bounding box of a recorded ServerPicture. */

#ifndef PICTURE_BOUNDING_BOX_H
#define PICTURE_BOUNDING_BOX_H


class BRect;
class DrawState;
class ServerPicture;


/** @brief Plays back a ServerPicture solely to compute its bounding box. */
class PictureBoundingBoxPlayer {
public:
	class State;

public:
	/** @brief Plays the picture and computes the axis-aligned bounding box.
	 *  @param picture    The ServerPicture to evaluate.
	 *  @param drawState  Current drawing state used during playback.
	 *  @param outBoundingBox Receives the computed bounding rectangle. */
	static	void				Play(ServerPicture* picture,
									const DrawState* drawState,
									BRect* outBoundingBox);
};


#endif // PICTURE_BOUNDING_BOX_H
