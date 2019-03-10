/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "effectloaders.h"

#include "project/effect.h"
#include "project/transition.h"
#include "io/path.h"
#include "panels/panels.h"
#include "panels/effectcontrols.h"
#include "io/crossplatformlib.h"
#include "io/config.h"

#include <QDir>
#include <QXmlStreamReader>

#include <QDebug>

QMutex olive::effects_loaded;

#ifndef NO_FREI0R
#include <frei0r.h>
typedef void (*f0rGetPluginInfo)(f0r_plugin_info_t* info);
#endif

void load_internal_effects() {
  if (!olive::CurrentRuntimeConfig.shaders_are_enabled) qWarning() << "Shaders are disabled, some effects may be nonfunctional";

  EffectMeta em;

  // load internal effects
  em.path = ":/internalshaders";

  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Volume";
  em.internal = EFFECT_INTERNAL_VOLUME;
  olive::effects.append(em);

  em.name = "Pan";
  em.internal = EFFECT_INTERNAL_PAN;
  olive::effects.append(em);

  em.name = "VST Plugin 2.x";
  em.internal = EFFECT_INTERNAL_VST;
  olive::effects.append(em);

  em.name = "Tone";
  em.internal = EFFECT_INTERNAL_TONE;
  olive::effects.append(em);

  em.name = "Noise";
  em.internal = EFFECT_INTERNAL_NOISE;
  olive::effects.append(em);

  em.name = "Fill Left/Right";
  em.internal = EFFECT_INTERNAL_FILLLEFTRIGHT;
  olive::effects.append(em);

  em.subtype = EFFECT_TYPE_VIDEO;

  em.name = "Transform";
  em.category = "Distort";
  em.internal = EFFECT_INTERNAL_TRANSFORM;
  olive::effects.append(em);

  em.name = "Corner Pin";
  em.internal = EFFECT_INTERNAL_CORNERPIN;
  olive::effects.append(em);

  /*em.name = "Mask";
  em.internal = EFFECT_INTERNAL_MASK;
  olive::effects.append(em);*/

  em.name = "Shake";
  em.internal = EFFECT_INTERNAL_SHAKE;
  olive::effects.append(em);

  em.name = "Text";
  em.category = "Render";
  em.internal = EFFECT_INTERNAL_TEXT;
  olive::effects.append(em);

  em.name = "Timecode";
  em.internal = EFFECT_INTERNAL_TIMECODE;
  olive::effects.append(em);

  em.name = "Solid";
  em.internal = EFFECT_INTERNAL_SOLID;
  olive::effects.append(em);

  // internal transitions
  em.type = EFFECT_TYPE_TRANSITION;
  em.category = "";

  em.name = "Cross Dissolve";
  em.internal = TRANSITION_INTERNAL_CROSSDISSOLVE;
  olive::effects.append(em);

  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Linear Fade";
  em.internal = TRANSITION_INTERNAL_LINEARFADE;
  olive::effects.append(em);

  em.name = "Exponential Fade";
  em.internal = TRANSITION_INTERNAL_EXPONENTIALFADE;
  olive::effects.append(em);

  em.name = "Logarithmic Fade";
  em.internal = TRANSITION_INTERNAL_LOGARITHMICFADE;
  olive::effects.append(em);
}

void load_shader_effects() {
  QList<QString> effects_paths = get_effects_paths();

  for (int h=0;h<effects_paths.size();h++) {
    const QString& effects_path = effects_paths.at(h);
    QDir effects_dir(effects_path);
    if (effects_dir.exists()) {
      // Load XML metadata for GLSL shader effects
      QList<QString> entries = effects_dir.entryList(QStringList("*.xml"), QDir::Files);
      for (int i=0;i<entries.size();i++) {
        QFile file(effects_path + "/" + entries.at(i));
        if (!file.open(QIODevice::ReadOnly)) {
          qCritical() << "Could not open" << entries.at(i);
          return;
        }

        QXmlStreamReader reader(&file);
        while (!reader.atEnd()) {
          if (reader.name() == "effect") {
            QString effect_name = "";
            QString effect_cat = "";
            const QXmlStreamAttributes attr = reader.attributes();
            for (int j=0;j<attr.size();j++) {
              if (attr.at(j).name() == "name") {
                effect_name = attr.at(j).value().toString();
              } else if (attr.at(j).name() == "category") {
                effect_cat = attr.at(j).value().toString();
              }
            }
            if (!effect_name.isEmpty()) {
              EffectMeta em;
              em.type = EFFECT_TYPE_EFFECT;
              em.subtype = EFFECT_TYPE_VIDEO;
              em.name = effect_name;
              em.category = effect_cat;
              em.filename = file.fileName();
              em.path = effects_path;
              em.internal = -1;
              olive::effects.append(em);
            } else {
              qCritical() << "Invalid effect found in" << entries.at(i);
            }
            break;
          }
          reader.readNext();
        }

        file.close();
      }

      // Load blending mode shaders from file
      QList<QString> blend_mode_entries = effects_dir.entryList(QStringList("*.blend"), QDir::Files);
      for (int i=0;i<blend_mode_entries.size();i++) {
        BlendMode b;
        b.loaded = false;
        b.url = effects_dir.filePath(blend_mode_entries.at(i));
        b.name = QFileInfo(b.url).baseName();
        olive::blend_modes.append(b);
      }
    }
  }
}

void init_effects() {
  EffectInit* init_thread = new EffectInit();
  QObject::connect(init_thread, SIGNAL(finished()), init_thread, SLOT(deleteLater()));
  init_thread->start();
}

#ifndef NO_FREI0R
void load_frei0r_effects_worker(const QString& dir, EffectMeta& em, QVector<QString>& loaded_names) {
  QDir search_dir(dir);
  if (search_dir.exists()) {
    QList<QString> entry_list = search_dir.entryList(LibFilter(), QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    for (int j=0;j<entry_list.size();j++) {
      QString entry_path = search_dir.filePath(entry_list.at(j));
      if (QFileInfo(entry_path).isDir()) {
        load_frei0r_effects_worker(entry_path, em, loaded_names);
      } else {
        ModulePtr effect = LibLoad(entry_path);
        if (effect != nullptr) {
          f0rGetPluginInfo get_info_func = reinterpret_cast<f0rGetPluginInfo>(LibAddress(effect, "f0r_get_plugin_info"));
          if (get_info_func != nullptr) {
            f0r_plugin_info_t info;
            get_info_func(&info);

            if (!loaded_names.contains(info.name)
                && info.plugin_type == F0R_PLUGIN_TYPE_FILTER
                && info.color_model == F0R_COLOR_MODEL_RGBA8888) {
              em.name = info.name;
              em.path = dir;
              em.filename = entry_list.at(j);
              em.tooltip = QString("%1\n%2\n%3\n%4").arg(em.name, info.author, info.explanation, em.filename);

              loaded_names.append(em.name);

              olive::effects.append(em);
            }
//                        qDebug() << "Found:" << info.name << "by" << info.author;
          }
          LibClose(effect);
        }
//                qDebug() << search_dir.filePath(entry_list.at(j));
      }
    }
  }
}

void load_frei0r_effects() {
  QList<QString> effect_dirs = get_effects_paths();

  // add defined paths for frei0r plugins on unix
#if defined(__APPLE__) || defined(__linux__) || defined(__HAIKU__)
  effect_dirs.prepend("/usr/lib/frei0r-1");
  effect_dirs.prepend("/usr/local/lib/frei0r-1");
  effect_dirs.prepend(QDir::homePath() + "/.frei0r-1/lib");
#endif

  QString env_path(qgetenv("FREI0R_PATH"));
  if (!env_path.isEmpty()) effect_dirs.append(env_path);

  QVector<QString> loaded_names;

  // search for paths
  EffectMeta em;
  em.category = "Frei0r";
  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_VIDEO;
  em.internal = EFFECT_INTERNAL_FREI0R;

  for (int i=0;i<effect_dirs.size();i++) {
    load_frei0r_effects_worker(effect_dirs.at(i), em, loaded_names);
  }
}
#endif

void IncludeBlendingShader(BlendMode& b) {
  if (!b.loaded) {
    QFile blending_file(b.url);

    if (blending_file.open(QFile::ReadOnly)) {

      QTextStream stream(&blending_file);

      while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.length() > 0 && line.at(0) == '#') {

          if (line.startsWith("#olive name ")) {

            // The blending mode can specify its own name
            b.name = line.mid(12);

          } else if (line.startsWith("#pragma glslify: export(")) {

            // Get function name
            b.function_name = line.mid(24, line.length()-25);

          } else if (line.contains("require")) {

            // This function wanted to include an external shader

            int index_of_last_bracked = line.lastIndexOf('(') + 1;

            QString include_fn = line.mid(index_of_last_bracked, line.length() - index_of_last_bracked - 1);
            include_fn.append(".blend");
            QString include_path = QFileInfo(b.url).dir().filePath(include_fn);

            // see if this file is already in the blend modes list
            bool found = false;
            for (int i=0;i<olive::blend_modes.size();i++) {
              if (QFileInfo(olive::blend_modes.at(i).url) == QFileInfo(include_path)) {

                // It is, so we'll include it now rather than later
                IncludeBlendingShader(olive::blend_modes[i]);

                found = true;
                break;
              }
            }

            if (!found) {
              // include the file, but don't add it to the blend mode list
              BlendMode b;
              b.url = include_path;
              IncludeBlendingShader(b);
            }

          }

        } else {

          // Assume this line is code and add it to the shader
          olive::generated_blending_shader.append(line);
          olive::generated_blending_shader.append("\n");

        }
      }

      blending_file.close();

    } else {
      qWarning() << "Failed to open blending shader" << b.url;
    }

    b.loaded = true;
  }
}

void GenerateBlendingShader()
{
  olive::generated_blending_shader = "#version 110\n" // Start with GLSL version identifier
                                     "\n"
                                     "uniform sampler2D background;\n" // background (base) texture color
                                     "uniform sampler2D foreground;\n" // foreground (blend) texture color
                                     "varying vec2 vTexCoord;\n"       // texture coordinate
                                     "uniform float opacity;\n"        // foreground opacity setting
                                     "uniform int blendmode;\n"        // blending mode switcher
                                     "\n"
                                     "\n";

  // Import code from each blend mode file
  for (int i=0;i<olive::blend_modes.size();i++) {
    IncludeBlendingShader(olive::blend_modes[i]);
  }

  // Create monolithic switcher function

  olive::generated_blending_shader.append("vec3 blend(vec3 base, vec3 blend, float opacity) {\n");

  for (int i=0;i<olive::blend_modes.size();i++) {
    if (i == 0) {
      olive::generated_blending_shader.append("  if (blendmode == 0) {\n");
    } else {
      olive::generated_blending_shader.append(QString(" else if (blendmode == %1) {\n").arg(i));
    }

    olive::generated_blending_shader.append(QString("    return %1(base, blend, opacity);\n").arg(olive::blend_modes.at(i).function_name));
    olive::generated_blending_shader.append("  }");
  }

  // Write the main() function for the shader
  //
  // NOTE/FIXME: Unfortunately the current blending shaders (from https://github.com/jamieowen/glsl-blend) all seem to
  // be calculated for unassociated alpha, while Olive's internal pipeline largely functions in associated alpha.
  // Therefore for the blending shaders to work as expected, the alpha has to be unassociated at this stage.
  // Naturally, this sucks, but I'm not entirely sure what the solution is apart from rewriting the blending shaders
  // or maybe finding a new library.

  olive::generated_blending_shader.append("\n  return blend;\n" // default return value
                                          "}\n"
                                          "\n"
                                          "void main() {\n"
                                          "  vec4 bg_color = texture2D(background, vTexCoord);\n" // Get background texture color
                                          "  vec4 fg_color = texture2D(foreground, vTexCoord);\n" // Get foreground texture color
                                          "  if (fg_color.a > 0.0) {\n"
                                          "    float true_opacity = opacity * fg_color.a;\n"
                                          "    vec3 unmultipled_fg = max(vec3(0.0), min(vec3(1.0), fg_color.rgb / fg_color.a));\n"
                                          "    vec3 blended_rgb = blend(bg_color.rgb, unmultipled_fg, true_opacity);\n" // Use switcher function above to blend RGBs
                                          "    vec4 composite = vec4(blended_rgb, bg_color.a + true_opacity);\n"
                                          "    gl_FragColor = composite;\n"
                                          "  } else {\n"
                                          "    gl_FragColor = bg_color;\n"
                                          "  }\n"
                                          "}\n");
}

EffectInit::EffectInit() {
  olive::effects_loaded.lock();
}

void EffectInit::run() {
  qInfo() << "Initializing effects...";
  load_internal_effects();
  load_shader_effects();
#ifndef NO_FREI0R
  load_frei0r_effects();
#endif
  GenerateBlendingShader();
  olive::effects_loaded.unlock();
  qInfo() << "Finished initializing effects";
}
