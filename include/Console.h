/*!
 * \file   Console.h
 * \date:  04 July 2011
 * \author StefanP.MUC
 * \brief  Ingame console
 */

#ifndef CONSOLE_H_
#define CONSOLE_H_

#include <list>
#include <vector>

#include <OgreFrameListener.h>
#include <Ogre.h>
#include <OIS/OIS.h>

#include "Gui.h"

class Console :
        public Ogre::Singleton<Console>,
        public Ogre::FrameListener
{
    public:
        Console();
        ~Console();

        inline void setVisible(const bool& newState){visible = newState; Gui::getSingleton().setVisible(!visible);};
        inline const bool& isVisible() const{return visible;}
        inline void toggleVisibility(){visible = !visible; Gui::getSingleton().setVisible(!visible);}

        void print(const Ogre::String &text);

        virtual bool frameStarted(const Ogre::FrameEvent &evt);
        virtual bool frameEnded(const Ogre::FrameEvent &evt);

        void onKeyPressed(const OIS::KeyEvent &arg);

        void addCommand(const Ogre::String &command, void (*)(std::vector<Ogre::String>&));
        void removeCommand(const Ogre::String &command);

    private:
        unsigned int consoleLineLength;
        unsigned int consoleLineCount;
        bool visible;
        bool initialized;
        Ogre::Rectangle2D* rect;
        Ogre::SceneNode* node;
        Ogre::OverlayElement* textbox;
        Ogre::Overlay* overlay;

        float height;
        bool updateOverlay;
        int startLine;
        std::list<Ogre::String> lines;
        Ogre::String prompt;
        std::map<Ogre::String, void(*)(std::vector<Ogre::String>&)> commands;
};

#endif /* CONSOLE_H_ */
