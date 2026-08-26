#ifndef DSINFER_TOOLS_CLI_SYNTHESISINPUT_H
#define DSINFER_TOOLS_CLI_SYNTHESISINPUT_H

#include <filesystem>
#include <memory>
#include <string>

#include <synthrt/Support/Expected.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds::cli {

    /// Input for one CLI synthesis request.
    ///
    /// The input file has the following JSON structure. Every field except \c singer is optional.
    ///
    /// <pre>
    /// {
    ///   "singer": "singer-contribution-id",
    ///   "duration": 1.5,
    ///   "steps": 20,
    ///   "depth": 0.8,
    ///   "words": [
    ///     {
    ///       "phones": [
    ///         {
    ///           "token": "n",
    ///           "language": "zh",
    ///           "tone": 2,
    ///           "start": 0.0,
    ///           "speakers": [
    ///             {"name": "main", "proportion": 1.0}
    ///           ]
    ///         }
    ///       ],
    ///       "notes": [
    ///         {
    ///           "key": "C4+0",
    ///           "cents": 0,
    ///           "duration": 1.5,
    ///           "glide": "none",
    ///           "is_rest": false
    ///         }
    ///       ]
    ///     }
    ///   ],
    ///   "parameters": [
    ///     {
    ///       "tag": "pitch",
    ///       "dynamic": true,
    ///       "values": [60.0, 60.1, 60.2],
    ///       "interval": 0.005,
    ///       "retake": {"start": 0.2, "end": 0.8}
    ///     },
    ///     {
    ///       "tag": "energy",
    ///       "dynamic": false,
    ///       "value": 1.0
    ///     }
    ///   ],
    ///   "speakers": [
    ///     {
    ///       "name": "main",
    ///       "dynamic": true,
    ///       "values": [1.0, 1.0, 1.0],
    ///       "interval": 0.005
    ///     }
    ///   ]
    /// }
    /// </pre>
    ///
    /// \c singer must be a nonempty contribution identifier. \c duration and \c depth are floating
    /// point numbers. \c steps is converted to an integer.
    ///
    /// \c words contains objects with optional \c phones and \c notes arrays. A phone may provide
    /// \c token, \c language, \c tone, \c start, and \c speakers. Each phone speaker requires
    /// \c name and defaults \c proportion to 1. A note key may be a MIDI integer, a fractional MIDI
    /// number, a string such as <tt>C4+0</tt> or <tt>D#4-25</tt>, or \c REST. \c cents is added to
    /// the value encoded by \c key. \c glide accepts \c up, \c down, or \c none.
    ///
    /// \c parameters accepts \c pitch, \c expr, \c f0, \c tone_shift, \c energy, \c breathiness,
    /// \c voicing, \c tension, \c mouth_opening, \c gender, and \c velocity. Unknown tags are
    /// ignored. A dynamic curve requires a numeric \c values array and a positive \c interval. A
    /// constant curve requires one numeric \c value. \c retake optionally selects a range with
    /// numeric \c start and \c end values.
    ///
    /// Top level \c speakers use the same constant or dynamic curve representation as parameters.
    /// Each speaker also requires \c name. Unknown object fields are preserved by the JSON parser
    /// but ignored by the input conversion.
    struct SynthesisInput {
        std::string singer;
        std::unique_ptr<Api::Acoustic::L1::AcousticStartInput> acoustic;

        /// Reads one synthesis request from a path.
        static srt::Expected<SynthesisInput> load(const std::filesystem::path &path);
    };

}

#endif // DSINFER_TOOLS_CLI_SYNTHESISINPUT_H
