/*
 // Copyright (c) 2023 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "Components/ColourPicker.h"

class PdTildeObject final : public TextBase {
public:
    static inline File pdLocation = File();

    PdTildeObject(t_gobj* ptr, Object* object)
        : TextBase(ptr, object)
    {
    }

    void mouseDown(MouseEvent const& e) override
    {
        if (!e.mods.isLeftButtonDown())
            return;

        if (getValue<bool>(object->locked)) {
            openPd();
        }
    }

    void openPd()
    {
        if (!pdLocation.exists()) {
            
            Dialogs::showOpenDialog([this](File& result){
                    if(!result.exists()) return;
                
        #if JUCE_MAC
                    if (result.hasFileExtension("app")) {
                        pdLocation = result.getChildFile("Contents").getChildFile("Resources");
                    } else if (result.isDirectory()) {
                        pdLocation = result;
                    } else {
                        return;
                    }
        #else
                    if (result.isDirectory()) {
                        pdLocation = result;
                    } else {
                        return;
                    }
        #endif
                    if (auto pdTilde = ptr.get<t_fake_pd_tilde>()) {
                        auto pdPath = pdLocation.getFullPathName();
                        auto schedPath = pdLocation.getChildFile("extra").getChildFile("pd~").getFullPathName();

                        pdTilde->x_pddir = gensym(pdPath.toRawUTF8());
                        pdTilde->x_schedlibdir = gensym(schedPath.toRawUTF8());
                        pd->sendDirectMessage(pdTilde.get(), "pd~", { "start" });
                    }
                }, true, true, "", "LastPdLocation");
        } else {
            if (auto pdTilde = ptr.get<t_fake_pd_tilde>()) {
                auto pdPath = pdLocation.getFullPathName();
                auto schedPath = pdLocation.getChildFile("extra").getChildFile("pd~").getFullPathName();

                pdTilde->x_pddir = gensym(pdPath.toRawUTF8());
                pdTilde->x_schedlibdir = gensym(schedPath.toRawUTF8());
                pd->sendDirectMessage(pdTilde.get(), "pd~", { "start" });
            }
        }
    }
};
