/**
 * This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief UNIRAST Defines
 * @file unirast.h
 * @author Neil 'Superna' Armstrong (c) 2010
 */

#ifndef _UNIRAST_H_
#define _UNIRAST_H_

enum unirast_color_space_e
{
    // Grayscale
    UNIRAST_COLOR_SPACE_GRAYSCALE_8BIT = 0,
    UNIRAST_COLOR_SPACE_GRAYSCALE_32BIT = 4,

    //RGB
    UNIRAST_COLOR_SPACE_SRGB_24BIT_1 = 1,
    UNIRAST_COLOR_SPACE_SRGB_24BIT_3 = 3,
    UNIRAST_COLOR_SPACE_SRGB_24BIT_5 = 5,
    UNIRAST_COLOR_SPACE_SRGB_32BIT = 2,

    //CMYK
    UNIRAST_COLOR_SPACE_CMYK_32BIT_64BIT = 6
};

enum unirast_duplex_mode_e
{
    UNIRAST_DUPLEX_MODE_0 = 0,
    UNIRAST_DUPLEX_MODE_1 = 1,
    UNIRAST_DUPLEX_MODE_2 = 2,
    UNIRAST_DUPLEX_MODE_3 = 3
};

enum unirast_quality_e
{
    UNIRAST_QUALITY_3 = 3,
    UNIRAST_QUALITY_4 = 4,
    UNIRAST_QUALITY_5 = 5
};

enum unirast_bpp_e
{
    UNIRAST_BPP_8BIT = 8,
    UNIRAST_BPP_24BIT = 24,
    UNIRAST_BPP_32BIT = 32,
    UNIRAST_BPP_64BIT = 64
};

#endif
