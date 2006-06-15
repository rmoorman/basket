/***************************************************************************
 *   Copyright (C) 2006 by Petri Damsten                                   *
 *   damu@iki.fi                                                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef PASSWORD_H
#define PASSWORD_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LIBGPGME

#include <passwordlayout.h>
#include <kdialogbase.h>

/**
	@author Petri Damsten <damu@iki.fi>
*/
class Password : public PasswordLayout
{
	Q_OBJECT
	public:
		Password(QWidget *parent, const char *name = 0);
		~Password();

	private slots:
		virtual void changeKey();
		virtual void clearKey();
};

class PasswordDlg : public KDialogBase
{
	Q_OBJECT
	public:
		PasswordDlg(QWidget *parent, const char *name = 0);
		~PasswordDlg();

		Password* w;

	protected slots:
		virtual void slotOk();
};

#endif // HAVE_LIBGPGME

#endif // PASSWORD_H

